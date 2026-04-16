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
 *   - Calls vchi_tv_init() once at startup and vchi_tv_uninit() at shutdown.
 *   - Registers one vc_tv_register_callback() and fans the events out to
 *     every subscribed client.
 *   - Handles synchronous RPC requests (display state, EDID, modes, HDCP …).
 *   - Systemd unit: install as dshal-tvsvc.service, start before ds-hal users.
 *
 * Build: see CMakeLists.txt target dshal-tvsvc-daemon
 */

#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "interface/vmcs_host/vc_vchi_gencmd.h"
#include "dshalLogger.h"
#include "dshalUtils.h"     /* vchi_tv_init / vchi_tv_uninit */
#include "dsTVSvcProto.h"

/* ------------------------------------------------------------------ */

typedef struct {
    int  fd;
    bool subscribed;
} client_slot_t;

static client_slot_t       gClients[TVSVC_MAX_CLIENTS];
static pthread_mutex_t     gClientsMutex = PTHREAD_MUTEX_INITIALIZER;
static int                 gListenFd     = -1;
static volatile sig_atomic_t gRunning    = 1;

/* ------------------------------------------------------------------ *
 * I/O helpers
 * ------------------------------------------------------------------ */
static int send_all(int fd, const void *buf, size_t len)
{
    const uint8_t *p = (const uint8_t *)buf;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) return -1;
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
        if (n <= 0) return -1;
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
 * TV callback — fires on the RTOS thread; broadcast to subscribers.
 * ------------------------------------------------------------------ */
static void daemon_tv_callback(void *userdata,
                               uint32_t reason,
                               uint32_t param1,
                               uint32_t param2)
{
    (void)userdata;

    tvsvc_evt_tv_callback_t evt = { reason, param1, param2 };
    tvsvc_msg_hdr_t hdr = {
        .version = TVSVC_VERSION,
        .cmd     = TVSVC_EVT_TV_CALLBACK,
        .req_id  = 0,
        .len     = (uint16_t)sizeof(evt)
    };

    pthread_mutex_lock(&gClientsMutex);
    for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
        if (gClients[i].fd < 0 || !gClients[i].subscribed)
            continue;
        if (send_all(gClients[i].fd, &hdr, sizeof(hdr)) != 0 ||
            send_all(gClients[i].fd, &evt, sizeof(evt))  != 0) {
            /* Client gone — evict it. */
            (void)close(gClients[i].fd);
            gClients[i].fd         = -1;
            gClients[i].subscribed = false;
        }
    }
    pthread_mutex_unlock(&gClientsMutex);
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

        tvsvc_resp_get_modes_t resp_hd = {
            .status          = (count < 0) ? count : 0,
            .preferred_group = (uint32_t)pref_group,
            .preferred_mode  = pref_mode,
            .count           = (count < 0) ? 0U : (uint32_t)count
        };
        size_t modes_bytes = resp_hd.count * sizeof(TV_SUPPORTED_MODE_NEW_T);
        uint16_t plen = (uint16_t)(sizeof(resp_hd) + modes_bytes);
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
    fprintf(stderr, "[dsTVSvcDaemon] starting\n");

    /* Sole owner of VCHI / TVService. */
    if (vchi_tv_init() != 0) {
        fprintf(stderr, "[dsTVSvcDaemon] vchi_tv_init failed\n");
        return EXIT_FAILURE;
    }
    vc_tv_register_callback(daemon_tv_callback, NULL);

    /* Create Unix domain socket. */
    (void)unlink(TVSVC_SOCK_PATH);
    gListenFd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (gListenFd < 0) {
        fprintf(stderr, "[dsTVSvcDaemon] socket: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TVSVC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(gListenFd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "[dsTVSvcDaemon] bind: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    (void)chmod(TVSVC_SOCK_PATH, 0660);

    if (listen(gListenFd, 8) != 0) {
        fprintf(stderr, "[dsTVSvcDaemon] listen: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    for (int i = 0; i < TVSVC_MAX_CLIENTS; i++) {
        gClients[i].fd         = -1;
        gClients[i].subscribed = false;
    }

    signal(SIGTERM, sig_handler);
    signal(SIGINT,  sig_handler);
    signal(SIGPIPE, SIG_IGN);

    fprintf(stderr, "[dsTVSvcDaemon] listening on %s\n", TVSVC_SOCK_PATH);

    /* ---- Main event loop (poll-based, single thread). ---- */
    struct pollfd pfds[1 + TVSVC_MAX_CLIENTS];

    while (gRunning) {
        int nfds = 0;
        pfds[nfds].fd     = gListenFd;
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
            fprintf(stderr, "[dsTVSvcDaemon] poll: %s\n", strerror(errno));
            break;
        }

        /* New connection? */
        if (pfds[0].revents & POLLIN) {
            int cfd = accept(gListenFd, NULL, NULL);
            if (cfd >= 0) {
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

        /* Client data / disconnect? */
        for (int p = 1; p < nfds; p++) {
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

    fprintf(stderr, "[dsTVSvcDaemon] shutting down\n");
    vc_tv_unregister_callback(daemon_tv_callback);
    vchi_tv_uninit();
    (void)close(gListenFd);
    (void)unlink(TVSVC_SOCK_PATH);
    return EXIT_SUCCESS;
}
