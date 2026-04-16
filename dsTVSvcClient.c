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
 * dsTVSvcClient.c
 *
 * IPC client implementation.  Each ds-hal library instance (process) holds
 * one persistent Unix socket connection to the TVService broker daemon.
 *
 * Threading model:
 *   - One background "reader thread" per process reads all incoming data.
 *   - Event pushes (TVSVC_EVT_*) are dispatched to registered callbacks
 *     from the reader thread; callbacks MUST NOT call back into this client.
 *   - Synchronous RPC calls are serialised by gRpcMutex; the caller blocks
 *     on gRpcCond until the reader thread delivers the response.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "dsTVSvcClient.h"
#include "dshalLogger.h"

/* ------------------------------------------------------------------ */

#define MAX_CB_SLOTS   8
#define RPC_TIMEOUT_S  5

/* Maximum response payload = get_modes header + 127 mode structs */
#define RESP_BUF_SIZE  (sizeof(tvsvc_resp_get_modes_t) + \
                        TVSVC_MAX_MODES_PER_REQ * sizeof(TV_SUPPORTED_MODE_NEW_T))

/* ------------------------------------------------------------------ *
 * Internal state
 * ------------------------------------------------------------------ */

/* RPC channel — single in-flight request at a time */
static int             gConnFd     = -1;
static pthread_mutex_t gRpcMutex   = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gRpcCond    = PTHREAD_COND_INITIALIZER;
static bool            gRespReady  = false;
static uint8_t         gRespBuf[sizeof(tvsvc_msg_hdr_t) + RESP_BUF_SIZE];
static uint16_t        gRespLen    = 0;

/* Reader thread */
static pthread_t       gReaderThread;
static bool            gReaderRunning = false;
static volatile bool   gReaderStop    = false;

/* Registered event callbacks */
typedef struct {
    tvsvc_client_cb_t cb;
    void             *userdata;
} cb_slot_t;

static cb_slot_t       gCallbacks[MAX_CB_SLOTS];
static pthread_mutex_t gCbMutex = PTHREAD_MUTEX_INITIALIZER;

/* Monotonically-increasing request ID */
static atomic_uint gReqId = 0;

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

/* ------------------------------------------------------------------ *
 * Event dispatch — called from reader thread (no gRpcMutex held)
 * ------------------------------------------------------------------ */
static void dispatch_event(const tvsvc_msg_hdr_t *hdr, const uint8_t *payload)
{
    if (hdr->cmd != TVSVC_EVT_TV_CALLBACK ||
        hdr->len < sizeof(tvsvc_evt_tv_callback_t))
        return;

    const tvsvc_evt_tv_callback_t *evt =
        (const tvsvc_evt_tv_callback_t *)payload;

    pthread_mutex_lock(&gCbMutex);
    for (int i = 0; i < MAX_CB_SLOTS; i++) {
        if (gCallbacks[i].cb) {
            gCallbacks[i].cb(gCallbacks[i].userdata,
                             evt->reason, evt->param1, evt->param2);
        }
    }
    pthread_mutex_unlock(&gCbMutex);
}

/* ------------------------------------------------------------------ *
 * Reader thread — reads messages from gConnFd until told to stop.
 *
 * Incoming messages fall into two categories:
 *   bit-7 set (0x80+) : event push — dispatch to callbacks
 *   TVSVC_RESP_FLAG   : RPC response — deliver to waiting caller
 * ------------------------------------------------------------------ */
static void *reader_thread(void *arg)
{
    (void)arg;
    hal_dbg("[TVSvcClient] reader thread started\n");

    while (!gReaderStop) {
        /* Take a snapshot of the fd under the lock. */
        pthread_mutex_lock(&gRpcMutex);
        int fd = gConnFd;
        pthread_mutex_unlock(&gRpcMutex);

        if (fd < 0) break;

        tvsvc_msg_hdr_t hdr;
        if (recv_all(fd, &hdr, sizeof(hdr)) != 0) {
            if (!gReaderStop)
                hal_warn("[TVSvcClient] reader: connection lost\n");
            break;
        }

        uint8_t *payload = NULL;
        if (hdr.len > 0) {
            payload = (uint8_t *)malloc(hdr.len);
            if (!payload || recv_all(fd, payload, hdr.len) != 0) {
                free(payload);
                if (!gReaderStop)
                    hal_warn("[TVSvcClient] reader: payload recv failed\n");
                break;
            }
        }

        if (hdr.cmd & 0x80U) {
            /* Asynchronous event push. */
            dispatch_event(&hdr, payload);
        } else if (hdr.cmd & TVSVC_RESP_FLAG) {
            /* Synchronous RPC response — wake waiting caller. */
            pthread_mutex_lock(&gRpcMutex);
            size_t copy = (hdr.len < RESP_BUF_SIZE) ? hdr.len : RESP_BUF_SIZE;
            memcpy(gRespBuf, &hdr, sizeof(hdr));
            if (payload && copy > 0)
                memcpy(gRespBuf + sizeof(hdr), payload, copy);
            gRespLen   = (uint16_t)hdr.len;
            gRespReady = true;
            pthread_cond_signal(&gRpcCond);
            pthread_mutex_unlock(&gRpcMutex);
        }

        free(payload);
    }

    hal_dbg("[TVSvcClient] reader thread exiting\n");
    return NULL;
}

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */
int tvsvc_client_connect(void)
{
    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd >= 0) {
        /* Already connected — idempotent. */
        pthread_mutex_unlock(&gRpcMutex);
        return 0;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        hal_err("[TVSvcClient] socket: %s\n", strerror(errno));
        pthread_mutex_unlock(&gRpcMutex);
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TVSVC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        hal_err("[TVSvcClient] connect to %s: %s\n",
                TVSVC_SOCK_PATH, strerror(errno));
        (void)close(fd);
        pthread_mutex_unlock(&gRpcMutex);
        return -1;
    }

    memset(gCallbacks, 0, sizeof(gCallbacks));
    gRespReady = false;
    gReaderStop = false;
    gConnFd = fd;
    pthread_mutex_unlock(&gRpcMutex);

    /* Start reader thread. */
    if (pthread_create(&gReaderThread, NULL, reader_thread, NULL) != 0) {
        hal_err("[TVSvcClient] pthread_create: %s\n", strerror(errno));
        pthread_mutex_lock(&gRpcMutex);
        (void)close(gConnFd);
        gConnFd = -1;
        pthread_mutex_unlock(&gRpcMutex);
        return -1;
    }
    gReaderRunning = true;

    /*
     * Subscribe to events: send SUBSCRIBE_EVENTS and wait for the ack.
     * gRpcMutex guards the send + wait sequence.
     */
    pthread_mutex_lock(&gRpcMutex);
    {
        tvsvc_msg_hdr_t sub_hdr = {
            TVSVC_VERSION, TVSVC_CMD_SUBSCRIBE_EVENTS,
            (uint16_t)atomic_fetch_add(&gReqId, 1U), 0
        };
        if (send_all(gConnFd, &sub_hdr, sizeof(sub_hdr)) != 0) {
            pthread_mutex_unlock(&gRpcMutex);
            hal_err("[TVSvcClient] failed to send SUBSCRIBE_EVENTS\n");
            tvsvc_client_disconnect();
            return -1;
        }
        struct timespec ts;
        (void)clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += RPC_TIMEOUT_S;
        while (!gRespReady) {
            if (pthread_cond_timedwait(&gRpcCond, &gRpcMutex, &ts) == ETIMEDOUT) {
                pthread_mutex_unlock(&gRpcMutex);
                hal_err("[TVSvcClient] timeout waiting for SUBSCRIBE_EVENTS ack\n");
                tvsvc_client_disconnect();
                return -1;
            }
        }
        gRespReady = false;
    }
    pthread_mutex_unlock(&gRpcMutex);

    hal_info("[TVSvcClient] connected to %s\n", TVSVC_SOCK_PATH);
    return 0;
}

void tvsvc_client_disconnect(void)
{
    /* Signal reader thread to stop and unblock its recv(). */
    gReaderStop = true;

    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd >= 0) {
        (void)shutdown(gConnFd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&gRpcMutex);

    if (gReaderRunning) {
        (void)pthread_join(gReaderThread, NULL);
        gReaderRunning = false;
    }

    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd >= 0) {
        (void)close(gConnFd);
        gConnFd = -1;
    }
    gReaderStop = false;
    pthread_mutex_unlock(&gRpcMutex);

    hal_info("[TVSvcClient] disconnected\n");
}

/* ------------------------------------------------------------------ *
 * Callback registration
 * ------------------------------------------------------------------ */
int tvsvc_client_register_callback(tvsvc_client_cb_t cb, void *userdata)
{
    if (!cb) return -1;
    pthread_mutex_lock(&gCbMutex);
    for (int i = 0; i < MAX_CB_SLOTS; i++) {
        if (!gCallbacks[i].cb) {
            gCallbacks[i].cb       = cb;
            gCallbacks[i].userdata = userdata;
            pthread_mutex_unlock(&gCbMutex);
            return 0;
        }
    }
    pthread_mutex_unlock(&gCbMutex);
    return -1; /* table full */
}

void tvsvc_client_unregister_callback(tvsvc_client_cb_t cb)
{
    pthread_mutex_lock(&gCbMutex);
    for (int i = 0; i < MAX_CB_SLOTS; i++) {
        if (gCallbacks[i].cb == cb) {
            gCallbacks[i].cb       = NULL;
            gCallbacks[i].userdata = NULL;
        }
    }
    pthread_mutex_unlock(&gCbMutex);
}

/* ------------------------------------------------------------------ *
 * Core RPC helper — serialises one request/response exchange.
 * Caller must NOT hold gRpcMutex.
 * ------------------------------------------------------------------ */
static int do_rpc(uint8_t cmd,
                  const void *req_payload, uint16_t req_len,
                  void *resp_payload_out,  size_t    resp_buf_size)
{
    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd < 0) {
        pthread_mutex_unlock(&gRpcMutex);
        return -ENOTCONN;
    }

    tvsvc_msg_hdr_t hdr = {
        TVSVC_VERSION, cmd,
        (uint16_t)atomic_fetch_add(&gReqId, 1U),
        req_len
    };

    if (send_all(gConnFd, &hdr, sizeof(hdr)) != 0 ||
        (req_len > 0 && send_all(gConnFd, req_payload, req_len) != 0)) {
        pthread_mutex_unlock(&gRpcMutex);
        return -EIO;
    }

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += RPC_TIMEOUT_S;

    gRespReady = false;
    while (!gRespReady) {
        int rc = pthread_cond_timedwait(&gRpcCond, &gRpcMutex, &ts);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&gRpcMutex);
            hal_err("[TVSvcClient] RPC timeout cmd=0x%02x\n", cmd);
            return -ETIMEDOUT;
        }
    }

    /* Copy response payload out before releasing the mutex. */
    const uint8_t *rpayload = gRespBuf + sizeof(tvsvc_msg_hdr_t);
    if (resp_payload_out && resp_buf_size > 0) {
        size_t copy = (gRespLen < resp_buf_size) ? gRespLen : resp_buf_size;
        memcpy(resp_payload_out, rpayload, copy);
    }
    uint16_t rlen = gRespLen;
    gRespReady = false;
    pthread_mutex_unlock(&gRpcMutex);

    return (int)rlen;
}

/* ------------------------------------------------------------------ *
 * Query / action implementations
 * ------------------------------------------------------------------ */

int tvsvc_client_get_display_state(TV_DISPLAY_STATE_T *state)
{
    tvsvc_resp_display_state_t r;
    memset(&r, 0, sizeof(r));
    int rc = do_rpc(TVSVC_CMD_GET_DISPLAY_STATE, NULL, 0, &r, sizeof(r));
    if (rc < 0 || (size_t)rc < sizeof(r))
        return -EIO;
    if (r.status != 0)
        return r.status;
    memcpy(state, &r.state, sizeof(*state));
    return 0;
}

int tvsvc_client_get_supported_modes(HDMI_RES_GROUP_T         group,
                                     TV_SUPPORTED_MODE_NEW_T *supported_modes,
                                     uint32_t                 max_supported_modes,
                                     HDMI_RES_GROUP_T        *preferred_group,
                                     uint32_t                *preferred_mode)
{
    tvsvc_req_get_modes_t req = {
        (uint32_t)group,
        (max_supported_modes < TVSVC_MAX_MODES_PER_REQ)
            ? max_supported_modes : TVSVC_MAX_MODES_PER_REQ
    };

    size_t resp_buf_size = sizeof(tvsvc_resp_get_modes_t) +
                           TVSVC_MAX_MODES_PER_REQ * sizeof(TV_SUPPORTED_MODE_NEW_T);
    uint8_t *resp_buf = (uint8_t *)malloc(resp_buf_size);
    if (!resp_buf)
        return -ENOMEM;

    int rc = do_rpc(TVSVC_CMD_GET_SUPPORTED_MODES, &req, (uint16_t)sizeof(req),
                    resp_buf, resp_buf_size);
    if (rc < 0 || (size_t)rc < sizeof(tvsvc_resp_get_modes_t)) {
        free(resp_buf);
        return (rc < 0) ? rc : -EIO;
    }

    const tvsvc_resp_get_modes_t *r = (const tvsvc_resp_get_modes_t *)resp_buf;
    if (r->status != 0) {
        free(resp_buf);
        return r->status;
    }

    if (preferred_group) *preferred_group = (HDMI_RES_GROUP_T)r->preferred_group;
    if (preferred_mode)  *preferred_mode  = r->preferred_mode;

    int count = (int)r->count;
    if (supported_modes && count > 0) {
        uint32_t copy_n = ((uint32_t)count < max_supported_modes)
                          ? (uint32_t)count : max_supported_modes;
        memcpy(supported_modes,
               resp_buf + sizeof(tvsvc_resp_get_modes_t),
               copy_n * sizeof(TV_SUPPORTED_MODE_NEW_T));
    }

    free(resp_buf);
    return count; /* matches vc_tv_hdmi_get_supported_modes_new return convention */
}

int tvsvc_client_ddc_read(uint32_t offset, uint32_t length, void *buffer)
{
    if (length > TVSVC_DDC_MAX_LEN)
        length = TVSVC_DDC_MAX_LEN;

    tvsvc_req_ddc_read_t req = { offset, length };
    size_t resp_buf_size = sizeof(tvsvc_resp_ddc_read_t) + TVSVC_DDC_MAX_LEN;
    uint8_t *resp_buf = (uint8_t *)malloc(resp_buf_size);
    if (!resp_buf)
        return -ENOMEM;

    int rc = do_rpc(TVSVC_CMD_DDC_READ, &req, (uint16_t)sizeof(req),
                    resp_buf, resp_buf_size);
    if (rc < 0 || (size_t)rc < sizeof(tvsvc_resp_ddc_read_t)) {
        free(resp_buf);
        return (rc < 0) ? rc : -EIO;
    }

    const tvsvc_resp_ddc_read_t *r = (const tvsvc_resp_ddc_read_t *)resp_buf;
    if (r->status != 0) {
        free(resp_buf);
        return r->status;
    }

    int actual = r->actual_len;
    if (buffer && actual > 0) {
        uint32_t copy_n = ((uint32_t)actual < length) ? (uint32_t)actual : length;
        memcpy(buffer, resp_buf + sizeof(tvsvc_resp_ddc_read_t), copy_n);
    }

    free(resp_buf);
    return actual; /* matches vc_tv_hdmi_ddc_read return convention */
}

int tvsvc_client_audio_supported(EDID_AudioFormat     format,
                                 int                    num_channels,
                                 EDID_AudioSampleRate  sample_rate,
                                 EDID_AudioSampleSize  sample_size)
{
    tvsvc_req_audio_supported_t req = {
        (uint32_t)format,
        (uint32_t)num_channels,
        (uint32_t)sample_rate,
        (uint32_t)sample_size
    };
    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_AUDIO_SUPPORTED, &req, (uint16_t)sizeof(req),
                    &r, sizeof(r));
    if (rc < 0) return rc;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_sdtv_power_on(SDTV_MODE_T mode, const SDTV_OPTIONS_T *options)
{
    tvsvc_req_sdtv_power_on_t req;
    memset(&req, 0, sizeof(req));
    req.mode = (uint32_t)mode;
    if (options)
        memcpy(&req.options, options, sizeof(*options));

    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_SDTV_POWER_ON, &req, (uint16_t)sizeof(req),
                    &r, sizeof(r));
    if (rc < 0) return rc;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_tv_power_off(void)
{
    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_TV_POWER_OFF, NULL, 0, &r, sizeof(r));
    if (rc < 0) return rc;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_hdmi_power_on_preferred(void)
{
    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_HDMI_POWER_ON_PREFERRED, NULL, 0, &r, sizeof(r));
    if (rc < 0) return rc;
    if (r.status != 0) return r.status;
    return r.result;
}
