/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * dsTVSvcDaemon.c
 *
 * Standalone TVService broker daemon.
 *
 * This process is the SOLE owner of the VCHI/TVService RTOS connection on
 * Raspberry Pi 4.  All ds-hal library instances (which may run in separate
 * processes) communicate with this daemon via Unix domain socket IPC.
 *
 * Responsibilities:
 *   - Calls vc_vchi_tv_init() once at startup and vc_vchi_tv_stop() at shutdown.
 *   - Registers one vc_tv_register_callback() and fans the events out to
 *     every subscribed client.
 *   - Handles synchronous RPC requests (display state, EDID, modes, HDCP …).
 *   - Systemd unit: install as dshal-tvsvc.service, start before ds-hal users.
 *
 * Build: see CMakeLists.txt target dshal-tvsvc-daemon
 */

#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "interface/vmcs_host/vc_vchi_gencmd.h"
#include "interface/vmcs_host/vc_tvservice.h"
#include "dshalLogger.h"
#include "dsTVSvcProto.h"

/* ------------------------------------------------------------------ */

typedef struct {
    int  fd;
    bool subscribed;
} client_slot_t;

static client_slot_t       gClients[TVSVC_MAX_CLIENTS];
static pthread_mutex_t     gClientsMutex = PTHREAD_MUTEX_INITIALIZER;
static int                 gListenFd     = -1;
static int                 gEventFd      = -1;
static volatile sig_atomic_t gRunning    = 1;

/* Event queue for non-blocking callback fanout */
#define MAX_PENDING_EVENTS  16
typedef struct {
    tvsvc_evt_tv_callback_t evt;
} pending_event_t;

static pending_event_t     gPendingEvents[MAX_PENDING_EVENTS];
static int                 gPendingHead  = 0; /* ring-buffer read index  */
static int                 gPendingCount = 0; /* number of filled slots  */
static pthread_mutex_t     gEventsMutex = PTHREAD_MUTEX_INITIALIZER;

/* VCHI instance — sole owner of TVService RTOS connection */
static VCHI_INSTANCE_T     gVchiInstance = NULL;
static VCHI_CONNECTION_T   *gVchiConnection = NULL;

/* ------------------------------------------------------------------ *
 * I/O helpers
 * ------------------------------------------------------------------ */
static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1;
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    while (len > 0) {
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return -1; /* clean EOF */
        p   += n;
        len -= (size_t)n;
    }
    return 0;
}

static void send_resp_simple(int fd, uint8_t cmd, uint16_t req_id,
                             int32_t status, int32_t result)
{
    tvsvc_resp_simple_t  r   = { status, result };
    tvsvc_msg_hdr_t      hdr = {
        TVSVC_VERSION,
        (uint8_t)(cmd | TVSVC_RESP_FLAG),
        req_id,
        (uint16_t)sizeof(r)
    };
    (void)send_all(fd, &hdr, sizeof(hdr));
    (void)send_all(fd, &r,   sizeof(r));
}

/* ------------------------------------------------------------------ *
 * TV callback — fires on the RTOS thread; enqueue event and signal.
 * No blocking I/O allowed here. Main thread will process the queue.
 * ------------------------------------------------------------------ */
static void daemon_tv_callback(void *userdata,
                               uint32_t reason,
                               uint32_t param1,
                               uint32_t param2)
{
    (void)userdata;

    /* Enqueue event for main thread to process.
     * Use trylock — blocking here would stall the RTOS callback thread. */
    if (pthread_mutex_trylock(&gEventsMutex) != 0) {
        hal_warn("[dsTVSvcDaemon] callback: events mutex busy, dropping event\n");
        return;
    }
    if (gPendingCount < MAX_PENDING_EVENTS) {
        int tail = (gPendingHead + gPendingCount) % MAX_PENDING_EVENTS;
        gPendingEvents[tail].evt.reason = reason;
        gPendingEvents[tail].evt.param1 = param1;
        gPendingEvents[tail].evt.param2 = param2;
        gPendingCount++;

        /* Signal main loop via eventfd. */
        if (gEventFd >= 0) {
            uint64_t add = 1;
            ssize_t wr = write(gEventFd, &add, sizeof(add));
            if (wr < 0 && errno != EAGAIN) {
                hal_err("[dsTVSvcDaemon] eventfd write failed: %s\n", strerror(errno));
            }
        }
    } else {
        hal_warn("[dsTVSvcDaemon] event queue overflow; dropping event\n");
    }
    pthread_mutex_unlock(&gEventsMutex);
}

/* ------------------------------------------------------------------ *
 * Request dispatcher — called with pre-read header + payload.
 * ------------------------------------------------------------------ */
static void handle_request(int fd, const tvsvc_msg_hdr_t *hdr,
                           const uint8_t *payload)
{
    switch (hdr->cmd) {

    /* ---- GET_DISPLAY_STATE ---------------------------------------- */
    case TVSVC_CMD_GET_DISPLAY_STATE: {
        tvsvc_resp_display_state_t r;
        memset(&r, 0, sizeof(r));
        r.status = vc_tv_get_display_state(&r.state);
        tvsvc_msg_hdr_t rhdr = {
            TVSVC_VERSION,
            (uint8_t)(hdr->cmd | TVSVC_RESP_FLAG),
            hdr->req_id,
            (uint16_t)sizeof(r)
        };
        (void)send_all(fd, &rhdr, sizeof(rhdr));
        (void)send_all(fd, &r,    sizeof(r));
        break;
    }

    /* ---- GET_SUPPORTED_MODES -------------------------------------- */
    case TVSVC_CMD_GET_SUPPORTED_MODES: {
        if (hdr->len < sizeof(tvsvc_req_get_modes_t)) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -EINVAL, 0);
            break;
        }
        const tvsvc_req_get_modes_t *req = (const tvsvc_req_get_modes_t *)payload;
        uint32_t max = (req->max_modes < TVSVC_MAX_MODES_PER_REQ)
                       ? req->max_modes : TVSVC_MAX_MODES_PER_REQ;

        TV_SUPPORTED_MODE_NEW_T *modes =
            (TV_SUPPORTED_MODE_NEW_T *)calloc(max, sizeof(*modes));
        if (!modes) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -ENOMEM, 0);
            break;
        }
        HDMI_RES_GROUP_T pref_group = HDMI_RES_GROUP_INVALID;
        uint32_t         pref_mode  = 0;
        int count = vc_tv_hdmi_get_supported_modes_new(
                        (HDMI_RES_GROUP_T)req->group, modes, (int)max,
                        &pref_group, &pref_mode);
        if (count > 0 && (uint32_t)count > max) {
            hal_err("[dsTVSvcDaemon] get_supported_modes returned count %d > requested max %u\n",
                    count, max);
            count = -EIO;
        }

        tvsvc_resp_get_modes_t resp_hd = {
            .status          = (count < 0) ? count : 0,
            .preferred_group = (uint32_t)pref_group,
            .preferred_mode  = pref_mode,
            .count           = (count < 0) ? 0U : (uint32_t)count
        };
        size_t modes_bytes = resp_hd.count * sizeof(TV_SUPPORTED_MODE_NEW_T);
        size_t total_payload = sizeof(resp_hd) + modes_bytes;
        /* Bounds check plen to prevent truncation to uint16_t when payload exceeds UINT16_MAX. */
        if (total_payload > UINT16_MAX) {
            hal_err("[dsTVSvcDaemon] get_supported_modes payload too large (%zu > UINT16_MAX)\n", total_payload);
            free(modes);
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -EIO, 0);
            break;
        }
        uint16_t plen = (uint16_t)total_payload;
        tvsvc_msg_hdr_t rhdr = {
            TVSVC_VERSION,
            (uint8_t)(hdr->cmd | TVSVC_RESP_FLAG),
            hdr->req_id,
            plen
        };
        (void)send_all(fd, &rhdr,    sizeof(rhdr));
        (void)send_all(fd, &resp_hd, sizeof(resp_hd));
        if (modes_bytes > 0)
            (void)send_all(fd, modes, modes_bytes);
        free(modes);
        break;
    }

    /* ---- DDC_READ ------------------------------------------------- */
    case TVSVC_CMD_DDC_READ: {
        if (hdr->len < sizeof(tvsvc_req_ddc_read_t)) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -EINVAL, 0);
            break;
        }
        const tvsvc_req_ddc_read_t *req = (const tvsvc_req_ddc_read_t *)payload;
        uint32_t want = (req->len < TVSVC_DDC_MAX_LEN) ? req->len : TVSVC_DDC_MAX_LEN;
        uint8_t *buf  = (uint8_t *)calloc(want, 1U);
        if (!buf) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -ENOMEM, 0);
            break;
        }
        int actual = vc_tv_hdmi_ddc_read(req->offset, (int)want, buf);
        tvsvc_resp_ddc_read_t resp_hd = {
            .status     = (actual < 0) ? actual : 0,
            .actual_len = (actual < 0) ? 0     : actual
        };
        uint16_t plen = (uint16_t)(sizeof(resp_hd) + (size_t)resp_hd.actual_len);
        tvsvc_msg_hdr_t rhdr = {
            TVSVC_VERSION,
            (uint8_t)(hdr->cmd | TVSVC_RESP_FLAG),
            hdr->req_id,
            plen
        };
        (void)send_all(fd, &rhdr,    sizeof(rhdr));
        (void)send_all(fd, &resp_hd, sizeof(resp_hd));
        if (resp_hd.actual_len > 0)
            (void)send_all(fd, buf, (size_t)resp_hd.actual_len);
        free(buf);
        break;
    }

    /* ---- AUDIO_SUPPORTED ----------------------------------------- */
    case TVSVC_CMD_AUDIO_SUPPORTED: {
        if (hdr->len < sizeof(tvsvc_req_audio_supported_t)) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -EINVAL, 0);
            break;
        }
        const tvsvc_req_audio_supported_t *req =
            (const tvsvc_req_audio_supported_t *)payload;
        int result = vc_tv_hdmi_audio_supported(
                         (EDID_AudioFormat)req->format,
                         (int)req->num_channels,
                         (EDID_AudioSampleRate)req->sample_rate,
                         (EDID_AudioSampleSize)req->sample_size);
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, result);
        break;
    }

    /* ---- SDTV_POWER_ON ------------------------------------------- */
    case TVSVC_CMD_SDTV_POWER_ON: {
        if (hdr->len < sizeof(tvsvc_req_sdtv_power_on_t)) {
            send_resp_simple(fd, hdr->cmd, hdr->req_id, -EINVAL, 0);
            break;
        }
        const tvsvc_req_sdtv_power_on_t *req =
            (const tvsvc_req_sdtv_power_on_t *)payload;
        SDTV_OPTIONS_T opts;
        memcpy(&opts, &req->options, sizeof(opts));
        int result = vc_tv_sdtv_power_on((SDTV_MODE_T)req->mode, &opts);
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, result);
        break;
    }

    /* ---- TV_POWER_OFF -------------------------------------------- */
    case TVSVC_CMD_TV_POWER_OFF: {
        int result = vc_tv_power_off();
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, result);
        break;
    }

    /* ---- HDMI_POWER_ON_PREFERRED --------------------------------- */
    case TVSVC_CMD_HDMI_POWER_ON_PREFERRED: {
        int result = vc_tv_hdmi_power_on_preferred();
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, result);
        break;
    }

    /* ---- GET_FREE_GFX_MEM --------------------------------------- */
    case TVSVC_CMD_GET_FREE_GFX_MEM: {
        char buffer[64] = {0};
        tvsvc_resp_gfx_mem_t r = { .status = 0, .memory = 0 };
        if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc") != 0) {
            r.status = -EIO;
        } else {
            buffer[sizeof(buffer) - 1] = '\0';
            char *equal = strchr(buffer, '=');
            r.memory = (uint64_t)strtoull(equal ? (equal + 1) : buffer, NULL, 10);
        }
        tvsvc_msg_hdr_t rhdr = {
            TVSVC_VERSION,
            (uint8_t)(hdr->cmd | TVSVC_RESP_FLAG),
            hdr->req_id,
            (uint16_t)sizeof(r)
        };
        (void)send_all(fd, &rhdr, sizeof(rhdr));
        (void)send_all(fd, &r, sizeof(r));
        break;
    }

    /* ---- GET_TOTAL_GFX_MEM -------------------------------------- */
    case TVSVC_CMD_GET_TOTAL_GFX_MEM: {
        char buffer[64] = {0};
        tvsvc_resp_gfx_mem_t r = { .status = 0, .memory = 0 };
        if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc_total") != 0) {
            r.status = -EIO;
        } else {
            buffer[sizeof(buffer) - 1] = '\0';
            char *equal = strchr(buffer, '=');
            r.memory = (uint64_t)strtoull(equal ? (equal + 1) : buffer, NULL, 10);
        }
        tvsvc_msg_hdr_t rhdr = {
            TVSVC_VERSION,
            (uint8_t)(hdr->cmd | TVSVC_RESP_FLAG),
            hdr->req_id,
            (uint16_t)sizeof(r)
        };
        (void)send_all(fd, &rhdr, sizeof(rhdr));
        (void)send_all(fd, &r, sizeof(r));
        break;
    }

    /* ---- SUBSCRIBE_EVENTS --------------------------------------- */
    case TVSVC_CMD_SUBSCRIBE_EVENTS: {
        pthread_mutex_lock(&gClientsMutex);
        for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
            if (gClients[i].fd == fd) {
                gClients[i].subscribed = true;
                break;
            }
        }
        pthread_mutex_unlock(&gClientsMutex);
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, 0);
        break;
    }

    /* ---- UNSUBSCRIBE_EVENTS ------------------------------------- */
    case TVSVC_CMD_UNSUBSCRIBE_EVENTS: {
        pthread_mutex_lock(&gClientsMutex);
        for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
            if (gClients[i].fd == fd) {
                gClients[i].subscribed = false;
                break;
            }
        }
        pthread_mutex_unlock(&gClientsMutex);
        send_resp_simple(fd, hdr->cmd, hdr->req_id, 0, 0);
        break;
    }

    default:
        send_resp_simple(fd, hdr->cmd, hdr->req_id, -ENOSYS, 0);
        break;
    }
}

/* ------------------------------------------------------------------ *
 * Client table management
 * ------------------------------------------------------------------ */
static int add_client(int fd)
{
    pthread_mutex_lock(&gClientsMutex);
    for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
        if (gClients[i].fd < 0) {
            gClients[i].fd         = fd;
            gClients[i].subscribed = false;
            pthread_mutex_unlock(&gClientsMutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&gClientsMutex);
    return -1; /* table full */
}

static void remove_client(int fd)
{
    pthread_mutex_lock(&gClientsMutex);
    for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
        if (gClients[i].fd == fd) {
            gClients[i].fd         = -1;
            gClients[i].subscribed = false;
            break;
        }
    }
    pthread_mutex_unlock(&gClientsMutex);
    (void)close(fd);
}

/* ---- Process pending callback events from main thread. ---- */
static void process_pending_events(void)
{
    pthread_mutex_lock(&gEventsMutex);
    while (gPendingCount > 0) {
        pending_event_t pending = gPendingEvents[gPendingHead];
        gPendingHead = (gPendingHead + 1) % MAX_PENDING_EVENTS;
        gPendingCount--;
        pthread_mutex_unlock(&gEventsMutex);

        /* Broadcast to all subscribed clients (outside event lock). */
        tvsvc_msg_hdr_t hdr = {
            .version = TVSVC_VERSION,
            .cmd     = TVSVC_EVT_TV_CALLBACK,
            .req_id  = 0,
            .len     = (uint16_t)sizeof(pending.evt)
        };

        pthread_mutex_lock(&gClientsMutex);
        for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
            if (gClients[i].fd < 0 || !gClients[i].subscribed)
                continue;
            if (send_all(gClients[i].fd, &hdr, sizeof(hdr)) != 0 ||
                send_all(gClients[i].fd, &pending.evt, sizeof(pending.evt)) != 0) {
                /* Client gone — evict it. */
                (void)close(gClients[i].fd);
                gClients[i].fd         = -1;
                gClients[i].subscribed = false;
            }
        }
        pthread_mutex_unlock(&gClientsMutex);

        pthread_mutex_lock(&gEventsMutex);
    }
    pthread_mutex_unlock(&gEventsMutex);
}

/* ------------------------------------------------------------------ *
 * Signal handler
 * ------------------------------------------------------------------ */
static void sig_handler(int sig)
{
    (void)sig;
    gRunning = 0;
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int main(void)
{
    int exit_code = EXIT_FAILURE;
    bool tv_inited = false;
    bool gencmd_inited = false;
    bool cb_registered = false;

    /* Initialize VCOS — required before VCHI/TVService */
    vcos_init();

    hal_info("[dsTVSvcDaemon] starting\n");

    /* Initialize VCHI before TVService */
    if (vchi_initialise(&gVchiInstance) != 0) {
        hal_err("[dsTVSvcDaemon] vchi_initialise failed\n");
        goto cleanup;
    }
    if (vchi_connect(NULL, 0, gVchiInstance) != 0) {
        hal_err("[dsTVSvcDaemon] vchi_connect failed\n");
        goto cleanup;
    }

    /* Sole owner of VCHI / TVService. */
    if (vc_vchi_tv_init(gVchiInstance, &gVchiConnection, 1) != 0) {
        hal_err("[dsTVSvcDaemon] vc_vchi_tv_init failed\n");
        goto cleanup;
    }
    tv_inited = true;

    /* Initialise gencmd service — required for GET_FREE/TOTAL_GFX_MEM. */
    vc_vchi_gencmd_init(gVchiInstance, &gVchiConnection, 1);
    gencmd_inited = true;

    /* Create eventfd before registering the firmware callback so any early
     * callback can signal the main loop rather than being lost to gEventFd==-1. */
    gEventFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (gEventFd < 0) {
        hal_err("[dsTVSvcDaemon] eventfd: %s\n", strerror(errno));
        goto cleanup;
    }

    vc_tv_register_callback(daemon_tv_callback, NULL);
    cb_registered = true;

    /* Extract the path from TVSVC_SOCK_PATH and if its a directory, create it */
    char sock_dir[PATH_MAX] = {0};
    strncpy(sock_dir, TVSVC_SOCK_PATH, sizeof(sock_dir) - 1);
    char *last_slash = strrchr(sock_dir, '/');
    if (last_slash) {
        *last_slash = '\0';
        /* Ignore error if directory already exists. */
        if (mkdir(sock_dir, 0755) != 0 && errno != EEXIST) {
            hal_err("[dsTVSvcDaemon] mkdir: %s\n", strerror(errno));
            goto cleanup;
        }
    }

    /* Create Unix domain socket. */
    (void)unlink(TVSVC_SOCK_PATH);
    gListenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (gListenFd < 0) {
        hal_err("[dsTVSvcDaemon] socket: %s\n", strerror(errno));
        goto cleanup;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TVSVC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(gListenFd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        hal_err("[dsTVSvcDaemon] bind: %s\n", strerror(errno));
        goto cleanup;
    }

    /* Restrict the control socket to the daemon owner only. */
    if (chmod(TVSVC_SOCK_PATH, 0600) != 0) {
        hal_err("[dsTVSvcDaemon] chmod socket: %s\n", strerror(errno));
        goto cleanup;
    }

    if (listen(gListenFd, 8) != 0) {
        hal_err("[dsTVSvcDaemon] listen: %s\n", strerror(errno));
        goto cleanup;
    }

    for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
        gClients[i].fd         = -1;
        gClients[i].subscribed = false;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGPIPE, SIG_IGN);

    hal_info("[dsTVSvcDaemon] listening on %s\n", TVSVC_SOCK_PATH);

    /* ---- Main event loop (poll-based, single thread). ---- */
    struct pollfd pfds[2 + TVSVC_MAX_CLIENTS];  /* listen fd, event fd, clients */

    while (gRunning) {
        int nfds = 0;
        pfds[nfds].fd     = gListenFd;
        pfds[nfds].events = POLLIN;
        nfds++;

        pfds[nfds].fd     = gEventFd;
        pfds[nfds].events = POLLIN;
        nfds++;

        pthread_mutex_lock(&gClientsMutex);
        for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
            if (gClients[i].fd >= 0) {
                pfds[nfds].fd     = gClients[i].fd;
                pfds[nfds].events = POLLIN;
                nfds++;
            }
        }
        pthread_mutex_unlock(&gClientsMutex);

        int ready = poll(pfds, (nfds_t)nfds, 1000 /* ms timeout */);
        if (ready < 0) {
            if (errno == EINTR) continue;
            hal_err("[dsTVSvcDaemon] poll: %s\n", strerror(errno));
            break;
        }

        /* New connection? */
        if (pfds[0].revents & POLLIN) {
            int cfd = accept4(gListenFd, NULL, NULL, SOCK_CLOEXEC);
            if (cfd >= 0) {
                /* Apply short socket timeouts so a slow/stalled client
                 * cannot block the main event loop indefinitely. */
                struct timeval snd_tv = { .tv_sec = 1, .tv_usec = 0 };
                struct timeval rcv_tv = { .tv_sec = 1, .tv_usec = 0 };
                (void)setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO,
                                 &snd_tv, sizeof(snd_tv));
                (void)setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO,
                                 &rcv_tv, sizeof(rcv_tv));
                if (add_client(cfd) != 0) {
                    fprintf(stderr,
                        "[dsTVSvcDaemon] client table full; rejecting fd=%d\n", cfd);
                    (void)close(cfd);
                } else {
                    fprintf(stderr,
                        "[dsTVSvcDaemon] client connected fd=%d\n", cfd);
                }
            }
        }

        /* Pending callback events? */
        if (pfds[1].revents & POLLIN) {
            uint64_t val;
            ssize_t rd = read(gEventFd, &val, sizeof(val));  /* drain eventfd */
            if (rd < 0 && errno != EAGAIN) {
                hal_err("[dsTVSvcDaemon] eventfd read failed: %s\n", strerror(errno));
            }
            process_pending_events();
        }

        /* Client data / disconnect? */
        for (int p = 2; p < nfds; p++) {
            if (!(pfds[p].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;

            int cfd = pfds[p].fd;
            if (pfds[p].revents & (POLLHUP | POLLERR)) {
                fprintf(stderr,
                    "[dsTVSvcDaemon] client fd=%d disconnected\n", cfd);
                remove_client(cfd);
                continue;
            }

            tvsvc_msg_hdr_t hdr;
            if (recv_all(cfd, &hdr, sizeof(hdr)) != 0) {
                fprintf(stderr,
                    "[dsTVSvcDaemon] client fd=%d recv hdr failed\n", cfd);
                remove_client(cfd);
                continue;
            }
            if (hdr.version != TVSVC_VERSION) {
                fprintf(stderr,
                    "[dsTVSvcDaemon] client fd=%d bad version %u\n",
                    cfd, hdr.version);
                remove_client(cfd);
                continue;
            }

            uint8_t *payload = NULL;
            if (hdr.len > 0) {
                if (hdr.len > 4096U) { /* sanity cap */
                    remove_client(cfd);
                    continue;
                }
                payload = (uint8_t *)malloc(hdr.len);
                if (!payload || recv_all(cfd, payload, hdr.len) != 0) {
                    free(payload);
                    remove_client(cfd);
                    continue;
                }
            }

            handle_request(cfd, &hdr, payload);
            free(payload);
        }
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    hal_info("[dsTVSvcDaemon] shutting down\n");
    if (cb_registered)
        vc_tv_unregister_callback(daemon_tv_callback);
    if (gencmd_inited)
        vc_gencmd_stop();
    if (tv_inited)
        vc_vchi_tv_stop();
    if (gVchiInstance != NULL)
        vchi_disconnect(gVchiInstance);
    if (gEventFd >= 0)
        (void)close(gEventFd);
    if (gListenFd >= 0)
        (void)close(gListenFd);
    (void)unlink(TVSVC_SOCK_PATH);
    return exit_code;
}
