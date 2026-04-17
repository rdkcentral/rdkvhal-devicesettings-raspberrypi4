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
 *   - Event pushes (TVSVC_EVT_*) are queued by reader thread and dispatched
 *     by a separate worker thread.
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
#define MAX_EVENT_QUEUE 32

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
static atomic_bool     gReaderRunning = ATOMIC_VAR_INIT(false);
static atomic_bool     gReaderStop    = ATOMIC_VAR_INIT(false);
static _Thread_local bool tIsReaderThread = false;

/* Event dispatcher thread */
typedef struct {
    tvsvc_evt_tv_callback_t evt;
} queued_event_t;

static pthread_t       gEventThread;
static atomic_bool     gEventThreadRunning = ATOMIC_VAR_INIT(false);
static atomic_bool     gEventStop          = ATOMIC_VAR_INIT(false);
static pthread_mutex_t gEventMutex         = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  gEventCond          = PTHREAD_COND_INITIALIZER;
static queued_event_t  gEventQueue[MAX_EVENT_QUEUE];
static int             gEventHead          = 0;
static int             gEventCount         = 0;

/* Registered event callbacks */
typedef struct {
    tvsvc_client_cb_t cb;
    void             *userdata;
} cb_slot_t;

static cb_slot_t       gCallbacks[MAX_CB_SLOTS];
static pthread_mutex_t gCbMutex = PTHREAD_MUTEX_INITIALIZER;

/* Monotonically-increasing request ID */
static atomic_uint gReqId = 0;

/* Expected response for the current in-flight RPC (protected by gRpcMutex) */
static uint16_t gExpectedReqId = 0;
static uint8_t  gExpectedCmd   = 0;
static bool     gRpcPending    = false;

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

/* ------------------------------------------------------------------ *
 * Event dispatch helpers
 * ------------------------------------------------------------------ */
static void dispatch_event_payload(const tvsvc_evt_tv_callback_t *evt)
{
    tvsvc_client_cb_t callbacks[MAX_CB_SLOTS] = {0};
    void *userdatas[MAX_CB_SLOTS] = {0};
    int count = 0;

    /* Copy callback table under lock, then invoke without holding the lock. */
    pthread_mutex_lock(&gCbMutex);
    for (int i = 0; i < MAX_CB_SLOTS; i++) {
        if (gCallbacks[i].cb) {
            callbacks[count] = gCallbacks[i].cb;
            userdatas[count] = gCallbacks[i].userdata;
            count++;
        }
    }
    pthread_mutex_unlock(&gCbMutex);

    for (int i = 0; i < count; i++) {
        callbacks[i](userdatas[i], evt->reason, evt->param1, evt->param2);
    }
}

static void queue_event(const tvsvc_msg_hdr_t *hdr, const uint8_t *payload)
{
    if (hdr->cmd != TVSVC_EVT_TV_CALLBACK ||
        hdr->len < sizeof(tvsvc_evt_tv_callback_t))
        return;

    const tvsvc_evt_tv_callback_t *evt =
        (const tvsvc_evt_tv_callback_t *)payload;

    pthread_mutex_lock(&gEventMutex);
    if (gEventCount >= MAX_EVENT_QUEUE) {
        hal_warn("[TVSvcClient] dropping event: queue full (%d)\n", MAX_EVENT_QUEUE);
        pthread_mutex_unlock(&gEventMutex);
        return;
    }

    int tail = (gEventHead + gEventCount) % MAX_EVENT_QUEUE;
    gEventQueue[tail].evt = *evt;
    gEventCount++;
    pthread_cond_signal(&gEventCond);
    pthread_mutex_unlock(&gEventMutex);
}

static void *event_thread(void *arg)
{
    (void)arg;
    hal_dbg("[TVSvcClient] event thread started\n");

    while (true) {
        queued_event_t qevt;

        pthread_mutex_lock(&gEventMutex);
        while (gEventCount == 0 && !atomic_load(&gEventStop)) {
            pthread_cond_wait(&gEventCond, &gEventMutex);
        }

        if (gEventCount == 0 && atomic_load(&gEventStop)) {
            pthread_mutex_unlock(&gEventMutex);
            break;
        }

        qevt = gEventQueue[gEventHead];
        gEventHead = (gEventHead + 1) % MAX_EVENT_QUEUE;
        gEventCount--;
        pthread_mutex_unlock(&gEventMutex);

        dispatch_event_payload(&qevt.evt);
    }

    atomic_store(&gEventThreadRunning, false);
    hal_dbg("[TVSvcClient] event thread exiting\n");
    return NULL;
}

static int start_event_thread(void)
{
    if (atomic_load(&gEventThreadRunning))
        return 0;

    atomic_store(&gEventStop, false);
    pthread_mutex_lock(&gEventMutex);
    gEventHead = 0;
    gEventCount = 0;
    pthread_mutex_unlock(&gEventMutex);

    int rc = pthread_create(&gEventThread, NULL, event_thread, NULL);
    if (rc != 0) {
        hal_err("[TVSvcClient] event thread create failed: %s\n", strerror(rc));
        return -rc;
    }
    atomic_store(&gEventThreadRunning, true);
    return 0;
}

static void stop_event_thread(void)
{
    if (!atomic_load(&gEventThreadRunning))
        return;

    atomic_store(&gEventStop, true);
    pthread_mutex_lock(&gEventMutex);
    pthread_cond_signal(&gEventCond);
    pthread_mutex_unlock(&gEventMutex);

    (void)pthread_join(gEventThread, NULL);

    pthread_mutex_lock(&gEventMutex);
    gEventHead = 0;
    gEventCount = 0;
    pthread_mutex_unlock(&gEventMutex);
}

static void reader_disconnect_fd(int fd, bool wake_rpc)
{
    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd == fd)
        gConnFd = -1;
    if (wake_rpc && gRpcPending) {
        /* Wake a blocked do_rpc() immediately on connection loss. */
        gRespLen = 0;
        gRespReady = true;
        pthread_cond_signal(&gRpcCond);
    }
    pthread_mutex_unlock(&gRpcMutex);
    (void)shutdown(fd, SHUT_RDWR);
    (void)close(fd);
}

/* ------------------------------------------------------------------ *
 * Reader thread — reads messages from gConnFd until told to stop.
 *
 * Incoming messages fall into two categories:
 *   bit-7 set (0x80+) : event push — enqueue for event worker
 *   TVSVC_RESP_FLAG   : RPC response — deliver to waiting caller
 * ------------------------------------------------------------------ */
static void *reader_thread(void *arg)
{
    /* fd is passed directly so we can receive the SUBSCRIBE ack before
     * gConnFd is published (it stays -1 until subscription completes). */
    int fd = (int)(intptr_t)arg;
    tIsReaderThread = true;
    hal_dbg("[TVSvcClient] reader thread started\n");

    while (!atomic_load(&gReaderStop)) {

        tvsvc_msg_hdr_t hdr;
        if (recv_all(fd, &hdr, sizeof(hdr)) != 0) {
            if (!atomic_load(&gReaderStop)) {
                hal_warn("[TVSvcClient] reader: connection lost\n");
                reader_disconnect_fd(fd, true);
            }
            break;
        }

        uint8_t *payload = NULL;
        if (hdr.len > 0) {
            if (hdr.len > 4096U) {
                hal_warn("[TVSvcClient] reader: oversized payload %u; disconnecting\n",
                         (unsigned)hdr.len);
                if (!atomic_load(&gReaderStop))
                    reader_disconnect_fd(fd, true);
                break;
            }
            payload = (uint8_t *)malloc(hdr.len);
            if (!payload || recv_all(fd, payload, hdr.len) != 0) {
                free(payload);
                if (!atomic_load(&gReaderStop)) {
                    hal_warn("[TVSvcClient] reader: payload recv failed\n");
                    reader_disconnect_fd(fd, true);
                }
                break;
            }
        }

        /* Validate protocol version before processing message. */
        if (hdr.version != TVSVC_VERSION) {
            if (!atomic_load(&gReaderStop))
                hal_warn("[TVSvcClient] reader: protocol version mismatch "
                         "(got %u, expected %u); disconnecting\n",
                         (unsigned int)hdr.version,
                         (unsigned int)TVSVC_VERSION);
            free(payload);
            reader_disconnect_fd(fd, true);
            break;
        }

        if (hdr.cmd & 0x80U) {
            /* Asynchronous event push. */
            queue_event(&hdr, payload);
        } else if (hdr.cmd & TVSVC_RESP_FLAG) {
            /* Synchronous RPC response — validate req_id before waking caller.
             * A response whose req_id doesn't match the current in-flight RPC
             * is a stale reply from a previously timed-out request; discard it. */
            pthread_mutex_lock(&gRpcMutex);
            if (gRpcPending && hdr.req_id == gExpectedReqId &&
                hdr.cmd == (uint8_t)(gExpectedCmd | TVSVC_RESP_FLAG)) {
                if (hdr.len > RESP_BUF_SIZE) {
                    hal_err("[TVSvcClient] oversized response "
                            "req_id=%u len=%u (max %u) — waking waiter with error\n",
                            (unsigned)hdr.req_id,
                            (unsigned)hdr.len,
                            (unsigned)RESP_BUF_SIZE);
                    gRespLen   = 0;
                    gRespReady = true;
                    pthread_cond_signal(&gRpcCond);
                } else {
                    memcpy(gRespBuf, &hdr, sizeof(hdr));
                    if (payload && hdr.len > 0)
                        memcpy(gRespBuf + sizeof(hdr), payload, hdr.len);
                    gRespLen   = hdr.len;
                    gRespReady = true;
                    pthread_cond_signal(&gRpcCond);
                }
            } else {
                hal_warn("[TVSvcClient] discarding stale response "
                         "req_id=%u (expected %u)\n",
                         (unsigned)hdr.req_id, (unsigned)gExpectedReqId);
            }
            pthread_mutex_unlock(&gRpcMutex);
        }

        free(payload);
    }

    /* Clean up thread state on exit to prevent leaks on reconnect. */
    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd == fd)
        gConnFd = -1;
    if (gRpcPending) {
        /* Wake a blocked do_rpc() immediately on connection loss. */
        gRespLen = 0;
        gRespReady = true;
        pthread_cond_signal(&gRpcCond);
    }
    pthread_mutex_unlock(&gRpcMutex);
    atomic_store(&gReaderRunning, false);
    tIsReaderThread = false;
    (void)close(fd);

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
        int err = errno;
        hal_err("[TVSvcClient] socket: %s\n", strerror(err));
        pthread_mutex_unlock(&gRpcMutex);
        return -err;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TVSVC_SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int err = errno;
        hal_err("[TVSvcClient] connect to %s: %s\n",
                TVSVC_SOCK_PATH, strerror(err));
        (void)close(fd);
        pthread_mutex_unlock(&gRpcMutex);
        return -err;
    }

    pthread_mutex_lock(&gCbMutex);
    memset(gCallbacks, 0, sizeof(gCallbacks));
    pthread_mutex_unlock(&gCbMutex);
    gRespReady = false;
    atomic_store(&gReaderStop, false);
    /* Keep gConnFd = -1 until reader thread is running and subscription succeeds. */
    pthread_mutex_unlock(&gRpcMutex);

    int rc = start_event_thread();
    if (rc != 0) {
        (void)close(fd);
        return rc;
    }

    /* Start reader thread — pass fd directly so it can receive the
     * SUBSCRIBE ack before gConnFd is published. */
    rc = pthread_create(&gReaderThread, NULL, reader_thread, (void *)(intptr_t)fd);
    if (rc != 0) {
        hal_err("[TVSvcClient] pthread_create: %s\n", strerror(rc));
        (void)close(fd);
        stop_event_thread();
        return -rc;
    }
    atomic_store(&gReaderRunning, true);

    /*
     * Subscribe to events: send SUBSCRIBE_EVENTS and wait for the ack.
     * Must hold temporary fd in local scope; only publish to gConnFd
     * after subscription succeeds, preventing concurrent RPCs from happening
     * before reader thread is ready.
     */
    pthread_mutex_lock(&gRpcMutex);
    {
        uint16_t sub_req_id = (uint16_t)atomic_fetch_add(&gReqId, 1U);
        tvsvc_msg_hdr_t sub_hdr = {
            TVSVC_VERSION, TVSVC_CMD_SUBSCRIBE_EVENTS, sub_req_id, 0
        };
        gExpectedReqId = sub_req_id;
        gExpectedCmd   = TVSVC_CMD_SUBSCRIBE_EVENTS;
        gRpcPending    = true;
        gRespReady     = false;
        if (send_all(fd, &sub_hdr, sizeof(sub_hdr)) != 0) {
            gRpcPending = false;
            pthread_mutex_unlock(&gRpcMutex);
            hal_err("[TVSvcClient] failed to send SUBSCRIBE_EVENTS\n");
            atomic_store(&gReaderStop, true);
            (void)shutdown(fd, SHUT_RDWR);
            if (atomic_load(&gReaderRunning)) {
                (void)pthread_join(gReaderThread, NULL);
                atomic_store(&gReaderRunning, false);
                /* Reader thread already closes fd in its exit path. */
            }
            stop_event_thread();
            return -EIO;
        }
        struct timespec ts;
        (void)clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += RPC_TIMEOUT_S;
        while (!gRespReady) {
            if (pthread_cond_timedwait(&gRpcCond, &gRpcMutex, &ts) == ETIMEDOUT) {
                gRpcPending = false;
                pthread_mutex_unlock(&gRpcMutex);
                hal_err("[TVSvcClient] timeout waiting for SUBSCRIBE_EVENTS ack\n");
                atomic_store(&gReaderStop, true);
                (void)shutdown(fd, SHUT_RDWR);
                if (atomic_load(&gReaderRunning)) {
                    (void)pthread_join(gReaderThread, NULL);
                    atomic_store(&gReaderRunning, false);
                    /* Reader thread already closes fd in its exit path. */
                }
                stop_event_thread();
                return -ETIMEDOUT;
            }
        }
        gRpcPending = false;
        gRespReady  = false;

        /* Only now publish the fd as connected, after reader thread is running
         * and subscription handshake is complete. */
        gConnFd = fd;
    }
    pthread_mutex_unlock(&gRpcMutex);

    hal_info("[TVSvcClient] connected to %s\n", TVSVC_SOCK_PATH);
    return 0;
}

void tvsvc_client_disconnect(void)
{
    /* Signal reader thread to stop and unblock its recv(). */
    atomic_store(&gReaderStop, true);

    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd >= 0) {
        (void)shutdown(gConnFd, SHUT_RDWR);
    }
    pthread_mutex_unlock(&gRpcMutex);

    if (atomic_load(&gReaderRunning)) {
        (void)pthread_join(gReaderThread, NULL);
        atomic_store(&gReaderRunning, false);
    }

    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd >= 0) {
        (void)close(gConnFd);
        gConnFd = -1;
    }
    atomic_store(&gReaderStop, false);
    pthread_mutex_unlock(&gRpcMutex);

    stop_event_thread();

    hal_info("[TVSvcClient] disconnected\n");
}

/* ------------------------------------------------------------------ *
 * Callback registration
 * ------------------------------------------------------------------ */
int tvsvc_client_register_callback(tvsvc_client_cb_t cb, void *userdata)
{
    if (!cb) return -EINVAL;
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
    return -ENOMEM; /* table full */
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
    if (tIsReaderThread) {
        hal_err("[TVSvcClient] RPC cmd=0x%02x called from reader-thread callback context\n", cmd);
        return -EDEADLK;
    }

    pthread_mutex_lock(&gRpcMutex);
    if (gConnFd < 0) {
        pthread_mutex_unlock(&gRpcMutex);
        return -ENOTCONN;
    }

    uint16_t req_id = (uint16_t)atomic_fetch_add(&gReqId, 1U);
    tvsvc_msg_hdr_t hdr = {
        TVSVC_VERSION, cmd, req_id, req_len
    };

    /* Record what we expect back before sending (reader may reply immediately). */
    gExpectedReqId = req_id;
    gExpectedCmd   = cmd;
    gRpcPending    = true;
    gRespReady     = false;

    if (send_all(gConnFd, &hdr, sizeof(hdr)) != 0 ||
        (req_len > 0 && send_all(gConnFd, req_payload, req_len) != 0)) {
        gRpcPending = false;
        pthread_mutex_unlock(&gRpcMutex);
        return -EIO;
    }

    struct timespec ts;
    (void)clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += RPC_TIMEOUT_S;

    while (!gRespReady) {
        int rc = pthread_cond_timedwait(&gRpcCond, &gRpcMutex, &ts);
        if (rc == ETIMEDOUT) {
            gRpcPending = false;
            pthread_mutex_unlock(&gRpcMutex);
            hal_err("[TVSvcClient] RPC timeout cmd=0x%02x\n", cmd);
            return -ETIMEDOUT;
        }
    }
    gRpcPending = false;

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
    if (!state)
        return -EINVAL;
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

    /* Validate response count against actual payload bytes to prevent out-of-bounds read. */
    size_t payload_bytes = (size_t)rc - sizeof(tvsvc_resp_get_modes_t);
    size_t available_modes = payload_bytes / sizeof(TV_SUPPORTED_MODE_NEW_T);
    uint32_t resp_count = r->count;
    if (resp_count > TVSVC_MAX_MODES_PER_REQ || (size_t)resp_count > available_modes) {
        hal_err("[TVSvcClient] get_supported_modes: count %u exceeds max or payload (max %u, avail %zu)\n",
                resp_count, TVSVC_MAX_MODES_PER_REQ, available_modes);
        free(resp_buf);
        return -EIO;
    }

    if (preferred_group) *preferred_group = (HDMI_RES_GROUP_T)r->preferred_group;
    if (preferred_mode)  *preferred_mode  = r->preferred_mode;

    int count = (int)resp_count;
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

    /* Validate actual_len against payload bytes and protocol limits. */
    size_t payload_len = (size_t)rc - sizeof(tvsvc_resp_ddc_read_t);
    uint32_t max_actual = length;
    if (max_actual > TVSVC_DDC_MAX_LEN)
        max_actual = TVSVC_DDC_MAX_LEN;
    if ((size_t)max_actual > payload_len)
        max_actual = (uint32_t)payload_len;
    int actual = r->actual_len;
    if (actual < 0 || (uint32_t)actual > max_actual) {
        hal_err("[TVSvcClient] ddc_read: actual_len %d invalid (max %u, payload %zu)\n",
                actual, max_actual, payload_len);
        free(resp_buf);
        return -EIO;
    }

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
    if (rc < 0)
        return rc;
    if ((size_t)rc < sizeof(r))
        return -EIO;
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
    if (rc < 0)
        return rc;
    if ((size_t)rc < sizeof(r))
        return -EIO;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_tv_power_off(void)
{
    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_TV_POWER_OFF, NULL, 0, &r, sizeof(r));
    if (rc < 0)
        return rc;
    if ((size_t)rc < sizeof(r))
        return -EIO;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_hdmi_power_on_preferred(void)
{
    tvsvc_resp_simple_t r = {0};
    int rc = do_rpc(TVSVC_CMD_HDMI_POWER_ON_PREFERRED, NULL, 0, &r, sizeof(r));
    if (rc < 0)
        return rc;
    if ((size_t)rc < sizeof(r))
        return -EIO;
    if (r.status != 0) return r.status;
    return r.result;
}

int tvsvc_client_get_free_graphics_memory(uint64_t *memory)
{
    if (!memory)
        return -EINVAL;

    tvsvc_resp_gfx_mem_t r;
    memset(&r, 0, sizeof(r));
    int rc = do_rpc(TVSVC_CMD_GET_FREE_GFX_MEM, NULL, 0, &r, sizeof(r));
    if (rc < 0 || (size_t)rc < sizeof(r))
        return (rc < 0) ? rc : -EIO;
    if (r.status != 0)
        return r.status;

    *memory = r.memory;
    return 0;
}

int tvsvc_client_get_total_graphics_memory(uint64_t *memory)
{
    if (!memory)
        return -EINVAL;

    tvsvc_resp_gfx_mem_t r;
    memset(&r, 0, sizeof(r));
    int rc = do_rpc(TVSVC_CMD_GET_TOTAL_GFX_MEM, NULL, 0, &r, sizeof(r));
    if (rc < 0 || (size_t)rc < sizeof(r))
        return (rc < 0) ? rc : -EIO;
    if (r.status != 0)
        return r.status;

    *memory = r.memory;
    return 0;
}
