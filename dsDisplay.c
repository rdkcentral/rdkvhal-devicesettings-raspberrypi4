/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <poll.h>
#include <libudev.h>

#include "dsTypes.h"
#include "dsDisplay.h"
#include "dsAudio.h"
#include "dsUtl.h"
#include "dsError.h"
#include "dshalLogger.h"
#include "dshalEdidParser.h"

extern dsVideoPortResolution_t kResolutionsSettings[];
extern size_t kNumResolutionsSettings;

#include "dshalUtils.h"
#include "halif-versions.h"

dsDisplayEventCallback_t _halcallback = NULL;
extern dsAudioOutPortConnectCB_t _halhdmiaudioCB;
static void (*gConnectorChangeHook)(void) = NULL;
static pthread_mutex_t gHdmiWatcherMutex = PTHREAD_MUTEX_INITIALIZER;
dsVideoPortResolution_t *HdmiSupportedResolution = NULL;
static unsigned int numSupportedResn = 0;
static bool _bDisplayInited = false;

extern pthread_mutex_t gHdmiAudioCbMutex;

static dsAudioOutPortConnectCB_t get_hdmi_audio_cb(void)
{
    dsAudioOutPortConnectCB_t cb;
    pthread_mutex_lock(&gHdmiAudioCbMutex);
    cb = _halhdmiaudioCB;
    pthread_mutex_unlock(&gHdmiAudioCbMutex);
    return cb;
}

void dsRegisterConnectorChangeHook(void (*hook)(void))
{
    pthread_mutex_lock(&gHdmiWatcherMutex);
    gConnectorChangeHook = hook;
    pthread_mutex_unlock(&gHdmiWatcherMutex);
}

static void notify_audio_hotplug(bool connected)
{
    dsAudioOutPortConnectCB_t cb = get_hdmi_audio_cb();
    if (cb != NULL) {
        cb(dsAUDIOPORT_TYPE_HDMI, 0, connected);
    }
}

/* Forward declaration used by watcher helpers defined before full struct body. */
typedef struct _VDISPHandle_t VDISPHandle_t;

static void resolve_drm_card_name(char *cardName, size_t len)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    const char *slash = strrchr(cardPath, '/');
    const char *base = (slash != NULL) ? (slash + 1) : cardPath;

    snprintf(cardName, len, "%s", base);
}

static bool drm_get_hdmi_connector_state(bool *connected, bool *enabled)
{
    return dsGetHdmiConnectorState(connected, enabled);
}

/* HDMI connection watcher thread state (udev + libdrm) */
static pthread_t gHdmiWatcherThread = (pthread_t)(-1);
static atomic_bool gHdmiWatcherRunning = ATOMIC_VAR_INIT(false);
static pthread_t gInitialStateReporterThread = (pthread_t)(-1);
static bool gLastHdmiConnected = false;
static struct udev *gUdevCtx = NULL;
static struct udev_monitor *gUdevMonitor = NULL;
static int gUdevFd = -1;

#define HDMI_CONNECT_SETTLE_MS_DEFAULT      (25)
#define HDMI_DISCONNECT_SETTLE_MS_DEFAULT   (75)
#define HDMI_SETTLE_MS_MAX                  (2000)
#define HDMI_CONNECT_DEBOUNCE_ENV           "DSHAL_HDMI_CONNECT_DEBOUNCE_MS"
#define HDMI_DISCONNECT_DEBOUNCE_ENV        "DSHAL_HDMI_DISCONNECT_DEBOUNCE_MS"

static int get_env_ms_or_default(const char *name, int fallback)
{
    const char *value = getenv(name);
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || (end != NULL && *end != '\0')) {
        hal_warn("Invalid %s='%s', using default %d ms\n", name, value, fallback);
        return fallback;
    }

    if (parsed < 0) {
        parsed = 0;
    } else if (parsed > HDMI_SETTLE_MS_MAX) {
        parsed = HDMI_SETTLE_MS_MAX;
    }

    return (int)parsed;
}

static struct timespec get_hdmi_settle_delay(bool connected)
{
    int connectDelayMs = get_env_ms_or_default(HDMI_CONNECT_DEBOUNCE_ENV, HDMI_CONNECT_SETTLE_MS_DEFAULT);
    int disconnectDelayMs = get_env_ms_or_default(HDMI_DISCONNECT_DEBOUNCE_ENV, HDMI_DISCONNECT_SETTLE_MS_DEFAULT);
    int delayMs = connected ? connectDelayMs : disconnectDelayMs;

    struct timespec ts;
    ts.tv_sec = delayMs / 1000;
    ts.tv_nsec = (long)(delayMs % 1000) * 1000000L;
    return ts;
}

static void* hdmi_watcher_thread(void *arg)
{
    int nativeHandle = (int)(intptr_t)arg;
    bool currentConnected = false, currentEnabled = false;
    bool lastConnected = false;
    bool lastEnabled = false;
    unsigned char eventData = 0;

    pthread_mutex_lock(&gHdmiWatcherMutex);
    lastConnected = gLastHdmiConnected;
    pthread_mutex_unlock(&gHdmiWatcherMutex);

    if (drm_get_hdmi_connector_state(&currentConnected, &currentEnabled)) {
        lastEnabled = currentEnabled;
    }

    hal_info("HDMI watcher thread (udev + libdrm) started\n");

    while (atomic_load(&gHdmiWatcherRunning)) {
        /* Wait for udev DRM hotplug notifications and poll periodically as a safety net. */
        struct pollfd pfd = {0};
        pfd.fd = gUdevFd;
        pfd.events = POLLIN;

        int poll_result = poll(&pfd, 1, 250);  /* 250ms timeout */
        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            struct udev_device *dev = udev_monitor_receive_device(gUdevMonitor);
            if (dev) {
                const char *subsystem = udev_device_get_subsystem(dev);
                const char *sysname = udev_device_get_sysname(dev);
                const char *action = udev_device_get_action(dev);

                if (subsystem && strcmp(subsystem, "drm") == 0) {
                    hal_dbg("udev DRM event: action=%s sysname=%s\n",
                            action ? action : "unknown",
                            sysname ? sysname : "unknown");
                }
                udev_device_unref(dev);
            }
        } else if (poll_result < 0 && errno != EINTR) {
            hal_err("poll error: %s\n", strerror(errno));
            struct timespec ts = {.tv_sec = 0, .tv_nsec = 1000000};  /* 1ms sleep on error */
            nanosleep(&ts, NULL);
        }

        /* Refresh connector state for both udev events and timeout wakeups. */
        if (drm_get_hdmi_connector_state(&currentConnected, &currentEnabled)) {
            bool verifiedConnected = currentConnected;
            bool verifiedEnabled = currentEnabled;

            /* Confirm connection transitions once more before notifying clients.
             * This filters transient drm samples seen during hotplug settle. */
            if (currentConnected != lastConnected) {
                const struct timespec verifyDelay = get_hdmi_settle_delay(currentConnected);
                if (verifyDelay.tv_sec != 0 || verifyDelay.tv_nsec != 0) {
                    nanosleep(&verifyDelay, NULL);
                }

                if (drm_get_hdmi_connector_state(&verifiedConnected, &verifiedEnabled)) {
                    if (verifiedConnected != currentConnected) {
                        hal_dbg("Ignoring transient HDMI state sample: connected=%d -> verified=%d\n",
                                currentConnected, verifiedConnected);
                        continue;
                    }
                    currentEnabled = verifiedEnabled;
                } else {
                    /* Skip reporting state transitions when verification fails. */
                    hal_dbg("Skipping HDMI transition event: verification read failed\n");
                    continue;
                }
            }

            bool stateChanged = false;
            bool connectionChanged = false;
            bool notifyConnected = false;
            dsDisplayEventCallback_t callback = NULL;

            pthread_mutex_lock(&gHdmiWatcherMutex);

            /* Detect state change and snapshot callback under lock. */
            if (currentConnected != lastConnected || currentEnabled != lastEnabled) {
                if (currentConnected != lastConnected) {
                    hal_info("HDMI connection state changed: connected=%d (was %d)\n", currentConnected, lastConnected);
                    connectionChanged = true;
                }
                if (currentEnabled != lastEnabled) {
                    hal_info("HDMI enabled state changed: enabled=%d (was %d)\n", currentEnabled, lastEnabled);
                }
                lastConnected = currentConnected;
                lastEnabled = currentEnabled;
                gLastHdmiConnected = currentConnected;
                callback = _halcallback;
                stateChanged = true;
                notifyConnected = currentConnected;
            }

            pthread_mutex_unlock(&gHdmiWatcherMutex);

            /* Invoke callbacks outside lock to avoid callback re-entry deadlock. */
            if (stateChanged) {
                if (connectionChanged && NULL != callback) {
                    if (notifyConnected) {
                        hal_dbg("HDMI cable connected, triggering CONNECTED event\n");
                        callback(nativeHandle, dsDISPLAY_EVENT_CONNECTED, &eventData);
                    } else {
                        hal_dbg("HDMI cable disconnected, triggering DISCONNECTED event\n");
                        callback(nativeHandle, dsDISPLAY_EVENT_DISCONNECTED, &eventData);
                    }
                } else if (connectionChanged) {
                    hal_warn("_halcallback is NULL, cannot report event\n");
                }

                if (connectionChanged) {
                    notify_audio_hotplug(notifyConnected);
                }

                void (*hook)(void) = NULL;
                pthread_mutex_lock(&gHdmiWatcherMutex);
                hook = gConnectorChangeHook;
                pthread_mutex_unlock(&gHdmiWatcherMutex);
                if (hook != NULL) {
                    hook();
                }
            }
        }
    }

    hal_info("HDMI watcher thread (udev + libdrm) terminated\n");
    return NULL;
}

static bool start_hdmi_watcher(int nativeHandle)
{
    if (atomic_load(&gHdmiWatcherRunning)) {
        hal_warn("HDMI watcher already running\n");
        return true;
    }

    bool initialConnected = false;
    bool initialEnabled = false;

    if (drm_get_hdmi_connector_state(&initialConnected, &initialEnabled)) {
        pthread_mutex_lock(&gHdmiWatcherMutex);
        gLastHdmiConnected = initialConnected;
        pthread_mutex_unlock(&gHdmiWatcherMutex);
        hal_info("Initial DRM HDMI state: connected=%d enabled=%d\n", initialConnected, initialEnabled);
    }

    gUdevCtx = udev_new();
    if (!gUdevCtx) {
        hal_err("Failed to initialize udev context\n");
        return false;
    }

    gUdevMonitor = udev_monitor_new_from_netlink(gUdevCtx, "udev");
    if (!gUdevMonitor) {
        hal_err("Failed to create udev monitor\n");
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
        return false;
    }

    if (udev_monitor_filter_add_match_subsystem_devtype(gUdevMonitor, "drm", NULL) < 0) {
        hal_err("Failed to add udev DRM filter\n");
        udev_monitor_unref(gUdevMonitor);
        gUdevMonitor = NULL;
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
        return false;
    }

    if (udev_monitor_enable_receiving(gUdevMonitor) < 0) {
        hal_err("Failed to enable udev monitor receiving\n");
        udev_monitor_unref(gUdevMonitor);
        gUdevMonitor = NULL;
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
        return false;
    }

    gUdevFd = udev_monitor_get_fd(gUdevMonitor);
    if (gUdevFd < 0) {
        hal_err("Failed to get udev monitor fd\n");
        udev_monitor_unref(gUdevMonitor);
        gUdevMonitor = NULL;
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
        return false;
    }

    atomic_store(&gHdmiWatcherRunning, true);
    int ret = pthread_create(&gHdmiWatcherThread, NULL, hdmi_watcher_thread, (void *)(intptr_t)nativeHandle);
    if (ret != 0) {
        hal_err("Failed to create HDMI watcher thread: %d\n", ret);
        atomic_store(&gHdmiWatcherRunning, false);
        gUdevFd = -1;
        udev_monitor_unref(gUdevMonitor);
        gUdevMonitor = NULL;
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
        return false;
    }

    hal_info("HDMI watcher thread (udev + libdrm) created successfully\n");
    return true;
}

static bool stop_hdmi_watcher(void)
{
    if (!atomic_load(&gHdmiWatcherRunning)) {
        return true;
    }

    atomic_store(&gHdmiWatcherRunning, false);

    if (gHdmiWatcherThread != (pthread_t)(-1)) {
        int ret = pthread_join(gHdmiWatcherThread, NULL);
        if (ret != 0) {
            hal_err("Failed to join HDMI watcher thread: %d\n", ret);
        }
        gHdmiWatcherThread = (pthread_t)(-1);
    }

    gUdevFd = -1;

    if (gUdevMonitor) {
        udev_monitor_unref(gUdevMonitor);
        gUdevMonitor = NULL;
    }

    if (gUdevCtx) {
        udev_unref(gUdevCtx);
        gUdevCtx = NULL;
    }

    hal_info("HDMI watcher thread (udev + libdrm) stopped\n");
    return true;
}

/* Structure to pass data to the initial state reporter thread */
typedef struct {
    int nativeHandle;
    dsDisplayEventCallback_t callback;
} initial_state_reporter_args_t;

/* One-shot thread to report the current HDMI state when callback is registered */
static void* report_initial_hdmi_state(void *arg)
{
    initial_state_reporter_args_t *args = (initial_state_reporter_args_t *)arg;
    int nativeHandle = args->nativeHandle;
    dsDisplayEventCallback_t callback = args->callback;
    bool currentConnected = false, currentEnabled = false;
    unsigned char eventData = 0;

    free(args);

    /* Small delay to ensure watcher thread is fully initialized */
    struct timespec ts = {0, 50 * 1000 * 1000}; /* 50ms */
    (void)nanosleep(&ts, NULL);

    hal_info("Initial state reporter thread: querying current HDMI state\n");

    /* Guard: skip callbacks if the display module was terminated during the delay. */
    pthread_mutex_lock(&gHdmiWatcherMutex);
    bool initialized = _bDisplayInited;
    pthread_mutex_unlock(&gHdmiWatcherMutex);

    if (!initialized) {
        hal_warn("Initial state reporter: display already terminated, skipping callback\n");
        hal_info("Initial state reporter thread: exiting\n");
        return NULL;
    }

    if (drm_get_hdmi_connector_state(&currentConnected, &currentEnabled)) {
        hal_info("Initial HDMI state: connected=%d enabled=%d\n", currentConnected, currentEnabled);
        if (callback != NULL) {
            if (currentConnected) {
                hal_dbg("Reporting initial HDMI CONNECTED state\n");
                callback(nativeHandle, dsDISPLAY_EVENT_CONNECTED, &eventData);
                notify_audio_hotplug(true);
            } else {
                hal_dbg("Reporting initial HDMI DISCONNECTED state\n");
                callback(nativeHandle, dsDISPLAY_EVENT_DISCONNECTED, &eventData);
                notify_audio_hotplug(false);
            }
        } else {
            notify_audio_hotplug(currentConnected);
        }
    } else {
        hal_err("Failed to query initial HDMI state\n");
    }

    hal_info("Initial state reporter thread: exiting\n");
    return NULL;
}


static dsError_t dsQueryHdmiResolution(const unsigned char *edid_raw, int edid_len);
static bool drm_get_preferred_hdmi_mode(char *mode, size_t len);
static dsVideoPortResolution_t *dsgetResolutionInfo(const char *res_name);

typedef struct {
    dsVideoPortResolution_t *hdmiSupportedResolution;
    unsigned int *numSupportedResn;
} HdmiResolutionParseContext_t;

typedef struct {
    dsDisplayEDID_t *edid;
} HdmiVsdbParseContext_t;

static bool parseHdmiVsdbFromCtaDataBlock(int tag,
        const unsigned char *data,
        int dataLen,
        void *context)
{
    HdmiVsdbParseContext_t *ctx = (HdmiVsdbParseContext_t *)context;

    if (ctx == NULL || ctx->edid == NULL || data == NULL) {
        return false;
    }

    if (tag != DSHAL_EDID_CTA_VENDOR_SPECIFIC_TAG || dataLen < 5) {
        return false;
    }

    /* HDMI VSDB OUI = 0x000C03, encoded in CTA payload as 03 0C 00 */
    if (data[0] == 0x03 && data[1] == 0x0C && data[2] == 0x00) {
        ctx->edid->hdmiDeviceType = true;
        ctx->edid->physicalAddressA = (data[3] >> 4) & 0x0F;
        ctx->edid->physicalAddressB = data[3] & 0x0F;
        ctx->edid->physicalAddressC = (data[4] >> 4) & 0x0F;
        ctx->edid->physicalAddressD = data[4] & 0x0F;
        ctx->edid->isRepeater = (ctx->edid->physicalAddressB != 0);
        return true;
    }

    return false;
}

static bool parseHdmiResolutionsFromCtaDataBlock(int tag,
        const unsigned char *data,
        int dataLen,
        void *context)
{
    HdmiResolutionParseContext_t *ctx = (HdmiResolutionParseContext_t *)context;

    if (ctx == NULL || ctx->hdmiSupportedResolution == NULL ||
            ctx->numSupportedResn == NULL || data == NULL) {
        return false;
    }

    if (tag != DSHAL_EDID_CTA_DATA_BLOCK_TAG_VIDEO || dataLen <= 0) {
        return false;
    }

    for (int s = 0; s < dataLen; s++) {
        int vic = data[s] & 0x7F;
        for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
            if (resolutionMap[i].mode != vic) {
                continue;
            }
            dsVideoPortResolution_t *res = dsgetResolutionInfo(resolutionMap[i].rdkRes);
            if (!res) {
                continue;
            }
            bool alreadyAdded = false;
            for (unsigned int j = 0; j < *(ctx->numSupportedResn); j++) {
                if (strncmp(ctx->hdmiSupportedResolution[j].name, res->name,
                            sizeof(ctx->hdmiSupportedResolution[j].name)) == 0) {
                    alreadyAdded = true;
                    break;
                }
            }
            if (!alreadyAdded) {
                memcpy(&ctx->hdmiSupportedResolution[*(ctx->numSupportedResn)], res,
                       sizeof(dsVideoPortResolution_t));
                hal_dbg("EDID resolution '%s' (VIC %d)\n",
                        ctx->hdmiSupportedResolution[*(ctx->numSupportedResn)].name, vic);
                (*(ctx->numSupportedResn))++;
            }
        }
    }

    return false;
}

typedef struct _VDISPHandle_t {
    dsVideoPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
} VDISPHandle_t;

static VDISPHandle_t _VDispHandles[dsVIDEOPORT_TYPE_MAX][2] = {};

bool dsIsValidVDispHandle(intptr_t m_handle) {
    for (int i = 0; i < dsVIDEOPORT_TYPE_MAX; i++) {
        //hal_info("Checking if m_handle(%p) is a match - &_VDispHandles[%d][0](%p).\n", m_handle, i, &_VDispHandles[i][0]);
        if ((intptr_t)&_VDispHandles[i][0] == m_handle) {
            hal_info("m_handle(%p) is a match.\n", m_handle);
            return true;
        }
    }
    return false;
}

static bool drm_get_preferred_hdmi_mode(char *mode, size_t len)
{
    return dsGetPreferredHdmiMode(mode, len);
}

/**
 * @brief Initialize underlying Video display units
 *
 * This function must initialize all the video display units and associated data structs
 *
 * @param None
 * @return dsError_t Error Code.
 */
dsError_t dsDisplayInit()
{
    hal_info("Invoked\n");
    if (true == _bDisplayInited) {
        return dsERR_ALREADY_INITIALIZED;
    }

    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType  = dsVIDEOPORT_TYPE_HDMI;
    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = dsVIDEOPORT_TYPE_HDMI;
    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;

    /* Query resolution information without TVService dependency. */
    dsQueryHdmiResolution(NULL, 0);

    /* Start HDMI connection watcher thread */
    if (!start_hdmi_watcher(_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle)) {
        hal_warn("Failed to start HDMI watcher thread, continuing without active monitoring\n");
    }

    _bDisplayInited = true;
    return dsERR_NONE;
}

/**
 * @brief To get the handle of the video display device
 *
 * This function is used to get the display handle of a given type
 *
 * @param [in] index     The index of the display device (0, 1, ...)
 * @param [out] *handle  The handle of video display device
 * @return dsError_t Error code.
 */
dsError_t dsGetDisplay(dsVideoPortType_t m_vType, int index, intptr_t *handle)
{
    hal_info("Invoked\n");
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }

    if (index != 0 || !dsVideoPortType_isValid(m_vType) || NULL == handle) {
        hal_err("Invalid params, index %d, m_vType %d, handle %p\n", index, m_vType, handle);
        return dsERR_INVALID_PARAM;
    }
    if (m_vType != dsVIDEOPORT_TYPE_HDMI) {
        hal_err("unsupported display type %d\n", m_vType);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    *handle = (intptr_t)&_VDispHandles[m_vType][index];

    return dsERR_NONE;
}

/**
 * @brief To get the aspect ration of the video display
 *
 * This function is used to get the aspect ratio that is set and used currently in
 * connected display device.
 *
 * @param [in] handle          Handle for the video display
 * @param [out] *aspectRatio   The Aspect ration that is used
 * @return dsError_t Error code.
 */
dsError_t dsGetDisplayAspectRatio(intptr_t handle, dsVideoAspectRatio_t *aspect)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    bool drmConnected = false, drmEnabled = false;
    char mode[64] = {0};
    unsigned int width = 0;
    unsigned int height = 0;

    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == aspect) {
        hal_err("Invalid params, handle %p, aspect %p\n", vDispHandle, aspect);
        return dsERR_INVALID_PARAM;
    }

    if (vDispHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        hal_err("Unsupported video port type %d\n", vDispHandle->m_vType);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* Check DRM connectivity before attempting to resolve aspect ratio. */
    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled) || !drmConnected) {
        hal_warn("HDMI not connected (DRM), cannot get aspect ratio\n");
        return dsERR_GENERAL;
    }

    /* Query active mode from westeros instead of EDID preferred mode */
    if (westerosGLConsoleRWWrapper("get mode", mode, sizeof(mode))) {
        /* Parse response format: "<status>: mode <modestring>" */
        char modeStr[128] = {0};
        int modeStatus = -1;
        if (sscanf(mode, "%d: mode %127s", &modeStatus, modeStr) == 2 && modeStatus == 0) {
            /* Extract dimensions from modeStr (e.g., "1920x1080i60") */
            if (sscanf(modeStr, "%ux%u", &width, &height) == 2 && height != 0) {
                /* Use strict > comparison to properly classify 4:3 modes */
                if ((unsigned long long)width * 3ULL > (unsigned long long)height * 4ULL) {
                    *aspect = dsVIDEO_ASPECT_RATIO_16x9;
                } else {
                    *aspect = dsVIDEO_ASPECT_RATIO_4x3;
                }
                hal_info("PortType:HDMI, active mode %s -> aspect ratio %d\n", modeStr, *aspect);
            } else {
                *aspect = dsVIDEO_ASPECT_RATIO_16x9;
                hal_warn("Unable to parse mode dimensions; defaulting aspect ratio to 16:9\n");
            }
        } else {
            *aspect = dsVIDEO_ASPECT_RATIO_16x9;
            hal_warn("Unable to get active mode; defaulting aspect ratio to 16:9\n");
        }
    } else if (drm_get_preferred_hdmi_mode(mode, sizeof(mode)) && sscanf(mode, "%ux%u", &width, &height) == 2 && height != 0) {
        /* Fallback to preferred mode if active mode unavailable */
        if ((unsigned long long)width * 3ULL > (unsigned long long)height * 4ULL) {
            *aspect = dsVIDEO_ASPECT_RATIO_16x9;
        } else {
            *aspect = dsVIDEO_ASPECT_RATIO_4x3;
        }
        hal_info("PortType:HDMI, DRM preferred mode %s -> aspect ratio %d\n", mode, *aspect);
    } else {
        *aspect = dsVIDEO_ASPECT_RATIO_16x9;
        hal_warn("Unable to read mode; defaulting aspect ratio to 16:9\n");
    }

    hal_dbg("Aspect ratio is %d\n", *aspect);
    return dsERR_NONE;
}


#if RDK_HALIF_DEVICE_SETTINGS_VERSION >= 0x05010000
/**
 * @brief Enables/Disables ALLM mode for HDMI output port connected to display.
 *
 * This function enables or disables the ALLM mode for specified HDMI output port.
 *
 * @param[in] handle    - Handle of the display device returned from dsGetDisplay()
 * @param[in] enabled   - Flag to enable/disable the ALLM mode for the HDMI output port
 *                         ( @a true to enable, @a false to disable)
 * @return dsError_t Error code.
 */
dsError_t dsSetAllmEnabled (intptr_t  handle, bool enabled)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) ) {
        hal_err("Invalid params, handle %p \n", vDispHandle );
        return dsERR_INVALID_PARAM;
    }

    /*
     * ALLM (Auto Low Latency Mode) is a feature defined in HDMI 2.1.
     * It allows a source device to signal a connected display to automatically switch to a low-latency, low-lag mode — often called "Game Mode".
     * Raspberry Pi firmware and drivers support basic HDMI 2.0a features (like 4K@60 ) but do not implement ALLM signaling.
     */
    hal_info("Allm operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Checks whether ALLM mode of HDMI output port connected to display is enabled or not.
 *
 * This function indicates whether ALLM mode for specified HDMI output port is enabled or not.
 * By default ALLM mode is disabled on bootup.
 *
 * @param[in]  handle   - Handle of the display device returned from dsGetDisplay()
 * @param[out] enabled  - Flag to hold the enabled status of ALLM mode for given HDMI output port.
 *                          ( @a true when ALLM mode is enabled or @a false otherwise)
 *
 * @return dsError_t Error code.
 */
dsError_t dsGetAllmEnabled (intptr_t  handle, bool *enabled)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == enabled ) {
        hal_err("Invalid params, handle %p enabled %p\n", vDispHandle, enabled );
        return dsERR_INVALID_PARAM;
    }

    /*
     * ALLM (Auto Low Latency Mode) is a feature defined in HDMI 2.1.
     * It allows a source device to signal a connected display to automatically switch to a low-latency, low-lag mode — often called "Game Mode".
     * Raspberry Pi firmware and drivers support basic HDMI 2.0a features (like 4K@60 ) but do not implement ALLM signaling.
     */
    hal_info("Allm operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Configures the AVI InfoFrame content type signalling for HDMI output port connected to display.
 *
 * For source devices, this function configures the AVI InfoFrame ITC, CN1 and CN0 bits.
 * AVI InfoFrame set remains until the power mode change or device reboot
 *
 * @param[in] handle      - Handle of the display device from dsGetDisplay()
 * @param[in] contentType - The content type (or none) to signal in the AVI InfoFrame.  Please refer ::dsAviContentType_t
 *
 * @return dsError_t Error code.
 */
dsError_t dsSetAVIContentType(intptr_t handle, dsAviContentType_t contentType)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) ) {
        hal_err("Invalid params, handle %p \n", vDispHandle );
        return dsERR_INVALID_PARAM;
    }

    /*
     * Raspberry Pi 4 is using the vc4 DRM driver with Firmware KMS (FKMS) backend.
     * vc4_fkms_ops refers to the "Firmware KMS" (FKMS) display pipeline used in the vc4 DRM (Direct Rendering Manager) driver on Raspberry Pi devices.
     * HDMI AVI InfoFrame fields are not exposed by vc4_fkms_ops or its underlying firmware.
     * So, We have defined HAL-level stubs that follow the expected structure and provide compatibility for higher layer.
     */
    hal_info("AVI ContentType operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the configured AVI InfoFrame content type signalling for HDMI output port connected to display.
 *
 * For source devices, this function gets the configuration of the AVI InfoFrame ITC, CN1 and CN0 bits.
 * By default, IT content is dsAVICONTENT_TYPE_NOT_SIGNALLED on bootup and after wakeup/resume.
 *
 * @param[in] handle       - Handle of the display device from dsGetDisplay()
 * @param[out] contentType - Pointer that receives the content type configuration set for AVI InfoFrames.
 * Please refer ::dsAviContentType_t
 *
 * @return dsError_t Error code.
 */
dsError_t dsGetAVIContentType(intptr_t handle, dsAviContentType_t* contentType)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == contentType ) {
        hal_err("Invalid params, handle %p contentType %p\n", vDispHandle, contentType );
        return dsERR_INVALID_PARAM;
    }

    /*
     * Raspberry Pi 4 is using the vc4 DRM driver with Firmware KMS (FKMS) backend.
     * vc4_fkms_ops refers to the "Firmware KMS" (FKMS) display pipeline used in the vc4 DRM (Direct Rendering Manager) driver on Raspberry Pi devices.
     * HDMI AVI InfoFrame fields are not exposed by vc4_fkms_ops or its underlying firmware.
     * So, We have defined HAL-level stubs that follow the expected structure and provide compatibility for higher layer.
     */
    hal_info("AVI ContentType operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Configures the AVI InfoFrame scan information signalling for HDMI output port connected to display.
 *
 * For source devices, this function configures the AVI InfoFrame S1 and S0 bits.
 * Source scan info (on AVI) is set only if Sink scan bit is set (on HF-VSDB) as per HDMI 2.1 Specification
 * AVI InfoFrame set remains until the power mode change or device reboot
 * This function return dsERR_OPERATION_NOT_SUPPORTED when when Sink doesn't support scan or HDMI disconnected
 *
 * @param[in] handle      - Handle of the display device from dsGetDisplay()
 * @param[in] scanInfo    - The scan information to signal in the AVI InfoFrame.  Please refer ::dsAVIScanInformation_t
 *
 * @return dsError_t Error code.
 */
dsError_t dsSetAVIScanInformation(intptr_t handle, dsAVIScanInformation_t scanInfo)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) ) {
        hal_err("Invalid params, handle %p \n", vDispHandle );
        return dsERR_INVALID_PARAM;
    }

    /*
     * Raspberry Pi 4 is using the vc4 DRM driver with Firmware KMS (FKMS) backend.
     * vc4_fkms_ops refers to the "Firmware KMS" (FKMS) display pipeline used in the vc4 DRM (Direct Rendering Manager) driver on Raspberry Pi devices.
     * HDMI AVI InfoFrame fields are not exposed by vc4_fkms_ops or its underlying firmware.
     * So, We have defined HAL-level stubs that follow the expected structure and provide compatibility for higher layer.
     */
    hal_info("AVI Scan Information operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the configured AVI InfoFrame scan information signalling for HDMI output port connected to display.
 *
 * For source devices, this function gets the configuration of the AVI InfoFrame S1 and S0 bits.
 * By default, scan info is dsAVI_SCAN_TYPE_NO_DATA on bootup and after wakeup/resume.
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle    - Handle of the display device from dsGetDisplay()
 * @param[out] scanInfo - Pointer that receives the scan information configuration set for AVI InfoFrames.  Please refer ::dsAVIScanInformation_t
 *
 */
dsError_t dsGetAVIScanInformation(intptr_t handle, dsAVIScanInformation_t* scanInfo)
{
    hal_info("invoked.\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == scanInfo ) {
        hal_err("Invalid params, handle %p scanInfo %p\n", vDispHandle, scanInfo );
        return dsERR_INVALID_PARAM;
    }

    /*
     * Raspberry Pi 4 is using the vc4 DRM driver with Firmware KMS (FKMS) backend.
     * vc4_fkms_ops refers to the "Firmware KMS" (FKMS) display pipeline used in the vc4 DRM (Direct Rendering Manager) driver on Raspberry Pi devices.
     * HDMI AVI InfoFrame fields are not exposed by vc4_fkms_ops or its underlying firmware.
     * So, We have defined HAL-level stubs that follow the expected structure and provide compatibility for higher layer.
     */
    hal_info("AVI Scan Information operations is not supported\n" );

    return dsERR_OPERATION_NOT_SUPPORTED;
}
#endif /* RDK_HALIF_DEVICE_SETTINGS_VERSION >= 0x05010000 */

/**
 * @brief Callback registration for display related events.
 *
 * This function registers a callback for display events corresponding to
 * the specified display device handle.
 *
 * @note Caller should install at most one callback function per handle.
 * Multiple listeners are supported at Caller layer and thus not
 * required in HAL implementation.
 *
 * @param[in] handle    - Handle of the display device
 * @param[in] cb        - Display Event callback function. Please refer ::dsDisplayEventCallback_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsDisplayEventCallback_t()
 *
 */
dsError_t dsRegisterDisplayEventCallback(intptr_t handle, dsDisplayEventCallback_t cb)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == cb || !dsIsValidVDispHandle((intptr_t)vDispHandle)) {
        hal_err("Invalid params, cb %p, handle %p\n", cb, vDispHandle);
        return dsERR_INVALID_PARAM;
    }
    /* Register The call Back */
    pthread_mutex_lock(&gHdmiWatcherMutex);
    if (NULL != _halcallback) {
        hal_warn("Callback already registered; override with new one.\n");
    }
    _halcallback = cb;
    pthread_mutex_unlock(&gHdmiWatcherMutex);

    /* Spawn a one-shot thread to report the current HDMI state immediately */
    initial_state_reporter_args_t *args = (initial_state_reporter_args_t *)malloc(sizeof(initial_state_reporter_args_t));
    if (args == NULL) {
        hal_err("Failed to allocate memory for initial state reporter thread args\n");
        return dsERR_GENERAL;
    }

    args->nativeHandle = vDispHandle->m_nativeHandle;
    args->callback = cb;

    pthread_t reporter_thread;
    int ret = pthread_create(&reporter_thread, NULL, report_initial_hdmi_state, (void *)args);
    if (ret != 0) {
        hal_err("Failed to create initial state reporter thread: %d\n", ret);
        free(args);
        return dsERR_GENERAL;
    }

    /* Store thread handle for joining in dsDisplayTerm() to prevent
     * callbacks from firing after display module termination. */
    gInitialStateReporterThread = reporter_thread;

    hal_dbg("Initial state reporter thread spawned\n");
    return dsERR_NONE;
}

/**
 * @brief Gets the EDID information from the specified display device.
 *
 * This function gets the EDID information from the HDMI/DVI display corresponding to
 * the specified display device handle.
 *
 * @param[in]  handle   - Handle of the display device
 * @param[out] edid     - EDID info of the specified display device. Please refer ::dsDisplayEDID_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDID(intptr_t handle, dsDisplayEDID_t *edid)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    bool drmConnected = false, drmEnabled = false;
    dsError_t ret = dsERR_NONE;
    size_t maxSupportedRes = 0;

    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == edid) {
        hal_err("Invalid params, handle %p, edid %p\n", vDispHandle, edid);
        return dsERR_INVALID_PARAM;
    }

    /* Ensure deterministic output for callers/tests that compare the full struct. */
    memset(edid, 0, sizeof(*edid));

    /* Check DRM connectivity before attempting EDID read */
    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled) || !drmConnected) {
        hal_warn("HDMI not connected (DRM), cannot read EDID\n");
        return dsERR_NONE;
    }

    unsigned char *raw = (unsigned char *)calloc(MAX_EDID_BYTES_LEN, sizeof(unsigned char));
    if (raw == NULL) {
        hal_err("Failed to allocate EDID buffer\n");
        return dsERR_GENERAL;
    }

    int length = 0;
    edid->numOfSupportedResolution = 0;
    if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        if (dsGetEDIDBytes(handle, raw, &length) != dsERR_NONE) {
            hal_err("Failed to get EDID bytes\n");
            ret = dsERR_GENERAL;
            goto cleanup;
        }
        hal_dbg("Raw EDID debug\n");
        EDID_t parsed_edid;
        parse_edid(raw, &parsed_edid);
        print_edid(&parsed_edid);
        edid->productCode = parsed_edid.product_code;
        edid->serialNumber = parsed_edid.serial_number;
        edid->manufactureWeek = parsed_edid.week_of_manufacture;
        edid->manufactureYear = parsed_edid.year_of_manufacture;
        edid->hdmiDeviceType = false;
        edid->isRepeater = false;
        edid->physicalAddressA = 0;
        edid->physicalAddressB = 0;
        edid->physicalAddressC = 0;
        edid->physicalAddressD = 0;
        /* Extract monitor name from EDID base block descriptor tag 0xFC.
         * Detailed timing descriptors: 4 × 18 bytes. Non-timing descriptor
         * header: [0]=0x00 [1]=0x00 [2]=0x00 [3]=tag [4]=0x00 [5..17]=data */
        strncpy(edid->monitorName, "Unknown", sizeof(edid->monitorName));
        edid->monitorName[dsEEDID_MAX_MON_NAME_LENGTH - 1] = '\0';
        hal_dbg("Searching for monitor name in EDID descriptors\n");
        for (int _d = 0; _d < 4; _d++) {
            const unsigned char *_desc = parsed_edid.detailed_timing_descriptors + _d * 18;
            hal_dbg("Descriptor %d: [0]=%02x [1]=%02x [2]=%02x [3]=%02x\n",
                _d, _desc[0], _desc[1], _desc[2], _desc[3]);
            if (_desc[0] == 0x00 && _desc[1] == 0x00 && _desc[2] == 0x00 && _desc[3] == 0xFC) {
                char _name[14] = {0};
                memcpy(_name, _desc + 5, 13);
                for (int _c = 12; _c >= 0 && (_name[_c] == '\n' || _name[_c] == ' '); _c--) {
                    _name[_c] = '\0';
                }
                if (_name[0] != '\0') {
                    strncpy(edid->monitorName, _name, sizeof(edid->monitorName));
                    edid->monitorName[dsEEDID_MAX_MON_NAME_LENGTH - 1] = '\0';
                    hal_info("Extracted monitor name from EDID descriptor: %s\n", edid->monitorName);
                }
                break;
            }
        }

        if (length > DSHAL_EDID_BLOCK_SIZE) {
            HdmiVsdbParseContext_t hdmiCtx = {
                .edid = edid,
            };
            (void)dshalEdidForEachCtaDataBlock(raw, length, parseHdmiVsdbFromCtaDataBlock, &hdmiCtx);
        }

        if (dsQueryHdmiResolution(raw, length) != dsERR_NONE) {
            hal_err("Failed to query HDMI resolution\n");
            ret = dsERR_GENERAL;
            goto cleanup;
        }
        hal_dbg("numSupportedResn from Table - %d\n", numSupportedResn);
        if (numSupportedResn == 0) {
            hal_err("No supported resolutions found\n");
            ret = dsERR_GENERAL;
            goto cleanup;
        }
        maxSupportedRes = sizeof(edid->suppResolutionList) / sizeof(edid->suppResolutionList[0]);
        if (numSupportedResn > maxSupportedRes) {
            hal_warn("Truncating supported resolution list from %u to %zu entries\n",
                    numSupportedResn, maxSupportedRes);
        }

        for (unsigned int i = 0; i < numSupportedResn && i < maxSupportedRes; i++) {
            memcpy(&edid->suppResolutionList[i], &HdmiSupportedResolution[i], sizeof(dsVideoPortResolution_t));
            hal_dbg("Copied resolution %s\n", edid->suppResolutionList[i].name);
        }
        edid->numOfSupportedResolution = (numSupportedResn < maxSupportedRes) ? numSupportedResn : maxSupportedRes;
    } else {
        hal_err("Handle type %d is not supported(not dsVIDEOPORT_TYPE_HDMI)\n", vDispHandle->m_vType);
        ret = dsERR_OPERATION_NOT_SUPPORTED;
        goto cleanup;
    }

cleanup:
    free(raw);
    return ret;
}

/**
 * @brief Terminate the usage of video display module
 *
 * This function will reset the data structs used within this module and release the video display specific handles
 *
 * @param None
 * @return dsError_t Error code.
 */
dsError_t dsDisplayTerm()
{
    hal_info("Invoked\n");
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }

    /* Clear initialized flag and callback pointer under mutex so that both
     * the initial-state reporter thread and the HDMI watcher thread stop
     * firing client callbacks before we join/stop either thread. */
    pthread_mutex_lock(&gHdmiWatcherMutex);
    _bDisplayInited = false;
    _halcallback = NULL;
    pthread_mutex_unlock(&gHdmiWatcherMutex);

    /* Join the initial state reporter thread to ensure it has exited before
     * we return and the caller potentially unloads the HAL. */
    if (gInitialStateReporterThread != (pthread_t)(-1)) {
        pthread_join(gInitialStateReporterThread, NULL);
        gInitialStateReporterThread = (pthread_t)(-1);
    }

    /* Stop HDMI connection watcher thread */
    stop_hdmi_watcher();

    if (HdmiSupportedResolution) {
        free(HdmiSupportedResolution);
        HdmiSupportedResolution = NULL;
    }
    return dsERR_NONE;
}

/**
 * @brief To get the native handle of the video display device
 *
 * This function is used to get the display handle of a given type
 *
 * @param [in] m_vType     Type of video display (HDMI, COMPONENT, ...)
 * @param [in] index     The index of the display device (0, 1, ...)
 * @param [out] *handle  The handle of video display device
 * @return dsError_t Error code.
 */
dsError_t dsDisplaygetNativeHandle(intptr_t handle, int *native)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == native) {
        hal_err("Invalid params, handle %p, native %p\n", vDispHandle, native);
        return dsERR_INVALID_PARAM;
    }
    if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI && vDispHandle->m_index == 0) {
        *native = vDispHandle->m_nativeHandle;
        return dsERR_NONE;
    } else {
        hal_warn("Failed to get native handle of type %d\n", vDispHandle->m_vType);
    }
    return dsERR_GENERAL;
}


/**
 *	Get The HDMI Resolution List
 *
 **/
static dsError_t dsQueryHdmiResolution(const unsigned char *edid_raw, int edid_len)
{
    hal_info("Invoked\n");

    if (HdmiSupportedResolution) {
        free(HdmiSupportedResolution);
        HdmiSupportedResolution = NULL;
    }

    numSupportedResn = 0;
    HdmiSupportedResolution = (dsVideoPortResolution_t *)malloc(sizeof(dsVideoPortResolution_t) * noOfItemsInResolutionMap);
    if (!HdmiSupportedResolution) {
        hal_err("malloc failed\n");
        return dsERR_GENERAL;
    }

    /* If we have EDID bytes, enumerate only resolutions whose VIC is advertised
     * in the CTA-861 Video Data Block(s) of the connected display. */
    if (edid_raw != NULL && edid_len >= DSHAL_EDID_BLOCK_SIZE) {
        HdmiResolutionParseContext_t ctx = {
            .hdmiSupportedResolution = HdmiSupportedResolution,
            .numSupportedResn = &numSupportedResn,
        };
        (void)dshalEdidForEachCtaDataBlock(edid_raw, edid_len, parseHdmiResolutionsFromCtaDataBlock, &ctx);

        if (numSupportedResn > 0) {
            hal_dbg("Total EDID-based HDMI resolutions = %u\n", numSupportedResn);
            return dsERR_NONE;
        }
        hal_warn("No VICs found in EDID extensions; falling back to static resolution map\n");
    }

    /* Static fallback: enumerate resolutionMap (deduped). */
    for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
        bool alreadyAdded = false;
        dsVideoPortResolution_t *resolution = dsgetResolutionInfo(resolutionMap[i].rdkRes);

        if (!resolution) {
            continue;
        }

        for (unsigned int j = 0; j < numSupportedResn; j++) {
            if (strncmp(HdmiSupportedResolution[j].name, resolution->name, sizeof(HdmiSupportedResolution[j].name)) == 0) {
                alreadyAdded = true;
                break;
            }
        }

        if (alreadyAdded) {
            continue;
        }

        memcpy(&HdmiSupportedResolution[numSupportedResn], resolution, sizeof(dsVideoPortResolution_t));
        hal_dbg("Static resolution '%s'\n", HdmiSupportedResolution[numSupportedResn].name);
        numSupportedResn++;
    }

    hal_dbg("Total Device supported resolutions on HDMI = %d\n", numSupportedResn);
    return dsERR_NONE;
}

static dsVideoPortResolution_t* dsgetResolutionInfo(const char *res_name)
{
    hal_info("Invoked\n");
    size_t iCount = kNumResolutionsSettings;

    /* Pass 1: Try exact match to avoid collapsing explicit-rate aliases.
     * (e.g., VIC 16 maps to "1080p", but we don't want to return "1080p24"
     * when looking up the exact token "1080p") */
    for (size_t i = 0; i < iCount; i++) {
        if (!strcmp(res_name, kResolutionsSettings[i].name)) {
            return &kResolutionsSettings[i];
        }
    }

    /* Pass 2: Prefix match fallback for bare/implicit-rate tokens or partial names
     * used in EDID enumeration (e.g., "1080p" matches first "1080p*" entry). */
    for (size_t i = 0; i < iCount; i++) {
        if (!strncmp(res_name, kResolutionsSettings[i].name, strlen(res_name))) {
            return &kResolutionsSettings[i];
        }
    }

    return NULL;
}

/**
 * @brief Gets the EDID buffer and EDID length of connected display device.
 *
 * This function is used to get the EDID buffer and EDID size of the connected display corresponding to
 * the specified display device handle.
 *
 * @param[in] handle    - Handle of the display device
 * @param[out] edid     - Pointer to raw EDID buffer
 * @param[out] length   - length of the EDID buffer data. Min value is 0
 *
 * @note Caller is responsible for allocating memory for edid( please refer ::MAX_EDID_BYTES_LEN ) and freeing the EDID buffer
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDIDBytes(intptr_t handle, unsigned char *edid, int *length)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    if (edid == NULL || length == NULL) {
        hal_err("invalid params\n");
        return dsERR_INVALID_PARAM;
    } else if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || vDispHandle != &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0]) {
        hal_err("invalid handle\n");
        return dsERR_INVALID_PARAM;
    }

    if (dsInternalGetHdmiEdidBytes(edid, length) != 0 || *length <= 0) {
        hal_err("Failed to get HDMI EDID bytes\n");
        *length = 0;
        return dsERR_GENERAL;
    }

#if 0 // Kept for debugging.
    FILE *file = fopen("/tmp/.hal-edid-bytes.dat", "wb");
    if (file != NULL) {
        for (int i = 0; i < *length; i++) {
            fprintf(file, "%02x", edid[i]);
        }
        fclose(file);
        hal_info("EDID bytes written to /tmp/.hal-edid-bytes.dat (%d bytes)\n", *length);
    } else {
        hal_err("Failed to open /tmp/.hal-edid-bytes.dat\n");
    }
#endif
    return dsERR_NONE;
}
