/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
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

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "dshalUtils.h"
#include "dshalLogger.h"

#define DSHALUTILS_EDID_MAX_BYTES (256)
#define DSHAL_DEFAULT_HDMI_MAX_BPC (10)
#define DSHAL_MIN_HDMI_MAX_BPC (8)
#define DSHAL_MAX_HDMI_MAX_BPC (12)

static int dsGetRequestedHdmiMaxBpc(void)
{
    const char *envMaxBpc = getenv("DSHAL_HDMI_MAX_BPC");
    int requestedMaxBpc = DSHAL_DEFAULT_HDMI_MAX_BPC;

    if (envMaxBpc != NULL && envMaxBpc[0] != '\0') {
        char *endPtr = NULL;
        long parsed = strtol(envMaxBpc, &endPtr, 10);
        if (endPtr != envMaxBpc && *endPtr == '\0') {
            requestedMaxBpc = (int)parsed;
        } else {
            hal_warn("Ignoring invalid DSHAL_HDMI_MAX_BPC='%s', using default %d\n",
                     envMaxBpc, DSHAL_DEFAULT_HDMI_MAX_BPC);
        }
    }

    if (requestedMaxBpc < DSHAL_MIN_HDMI_MAX_BPC) {
        requestedMaxBpc = DSHAL_MIN_HDMI_MAX_BPC;
    } else if (requestedMaxBpc > DSHAL_MAX_HDMI_MAX_BPC) {
        requestedMaxBpc = DSHAL_MAX_HDMI_MAX_BPC;
    }

    return requestedMaxBpc;
}

static int dsClampHdmiMaxBpc(int requestedMaxBpc)
{
    if (requestedMaxBpc < DSHAL_MIN_HDMI_MAX_BPC) {
        return DSHAL_MIN_HDMI_MAX_BPC;
    }
    if (requestedMaxBpc > DSHAL_MAX_HDMI_MAX_BPC) {
        return DSHAL_MAX_HDMI_MAX_BPC;
    }
    return requestedMaxBpc;
}

/**
 * @brief Resolve the DRM card name to use for HDMI operations.
 * @param[out] cardName Buffer to store the resolved DRM card name.
 * @param[in] len Length of the buffer.
 */
static void dsResolveDrmCardName(char *cardName, size_t len)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    const char *slash = strrchr(cardPath, '/');
    const char *base = (slash != NULL) ? (slash + 1) : cardPath;
    snprintf(cardName, len, "%s", base);
}

/**
 * @brief Open the DRM card device file as read-only and return its file descriptor.
 * @return File descriptor of the opened DRM card, or -1 on failure.
 */
int dsOpenDrmCardFd(void)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    /* Open read-only: the HAL only reads connector/crtc metadata via libdrm
     * (drmModeGetResources, drmModeGetConnectorCurrent) and never sets modes.
     * Opening O_RDWR makes the kernel treat this fd as a potential DRM master,
     * which races with westeros-gl's own master acquisition on the same card
     * during early boot, causing wstInitCtx to fail to enumerate connectors and
     * leading to a NULL-deref SIGSEGV in the WPEFramework worker pool. */
    int fd = open(cardPath, O_RDONLY);

    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFD);
        if (flags != -1) {
            (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }

    return fd;
}

/**
 * @brief Get the state of the HDMI connector.
 * @param[out] connected Pointer to a boolean to store the connection state.
 * @param[out] enabled Pointer to a boolean to store the enabled state.
 * @return true if the connector state was successfully retrieved, false otherwise.
 */
bool dsGetHdmiConnectorState(bool *connected, bool *enabled)
{
    int drmFd = -1;
    drmModeRes *resources = NULL;
    bool foundConnector = false;
    bool bestConnected = false;
    bool bestEnabled = false;
    int bestRank = -1;

    if (connected == NULL || enabled == NULL) {
        return false;
    }

    *connected = false;
    *enabled = false;

    drmFd = dsOpenDrmCardFd();
    if (drmFd < 0) {
        hal_err("Failed to open DRM card for connector state\n");
        return false;
    }

    resources = drmModeGetResources(drmFd);
    if (!resources) {
        close(drmFd);
        return false;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        bool entryConnected = false;
        bool entryEnabled = false;
        int entryRank = 0;
        drmModeConnector *connector = drmModeGetConnectorCurrent(drmFd, resources->connectors[i]);

        if (!connector) {
            connector = drmModeGetConnector(drmFd, resources->connectors[i]);
        }
        if (!connector) {
            continue;
        }

        if (connector->connector_type != DRM_MODE_CONNECTOR_HDMIA
#ifdef DRM_MODE_CONNECTOR_HDMIB
            && connector->connector_type != DRM_MODE_CONNECTOR_HDMIB
#endif
        ) {
            drmModeFreeConnector(connector);
            continue;
        }

        /* Ignore transient unknown state samples to avoid false
         * disconnect/reconnect notifications during hotplug settle. */
        if (connector->connection == DRM_MODE_UNKNOWNCONNECTION) {
            drmModeFreeConnector(connector);
            continue;
        }

        entryConnected = (connector->connection == DRM_MODE_CONNECTED);

        if (connector->encoder_id != 0) {
            drmModeEncoder *encoder = drmModeGetEncoder(drmFd, connector->encoder_id);
            if (encoder) {
                drmModeCrtc *crtc = drmModeGetCrtc(drmFd, encoder->crtc_id);
                if (crtc) {
                    entryEnabled = crtc->mode_valid;
                    drmModeFreeCrtc(crtc);
                }
                drmModeFreeEncoder(encoder);
            }
        }

        if (!entryEnabled && entryConnected && connector->count_modes > 0 && connector->encoder_id != 0) {
            entryEnabled = true;
        }

        /* Rank candidates to avoid false disconnects when multiple HDMI connectors
         * are present (for example HDMI-A-1 connected, HDMI-A-2 disconnected):
         *   2 = connected+enabled (best)
         *   1 = connected only
         *   0 = disconnected */
        entryRank = entryConnected ? (entryEnabled ? 2 : 1) : 0;

        if (!foundConnector || entryRank > bestRank) {
            bestConnected = entryConnected;
            bestEnabled = entryEnabled;
            bestRank = entryRank;
            foundConnector = true;
        }

        if (entryRank == 2) {
            drmModeFreeConnector(connector);
            break;
        }

        drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);
    close(drmFd);

    if (!foundConnector) {
        return false;
    }

    *connected = bestConnected;
    *enabled = bestEnabled;
    return true;
}

/**
 * @brief Apply a specific maximum bits per color (bpc) for HDMI outputs via the DRM
 * connector property "max bpc".  This mirrors what modetest does: enumerate connectors,
 * find the "max bpc" property ID, and set it with drmModeConnectorSetProperty().
 * A short-lived O_RDWR fd is used only for this call; drmSetMaster() is never called
 * so there is no master conflict with Westeros.
 * @param[in] requestedMaxBpc Requested HDMI max bpc value; values outside
 * DSHAL_MIN_HDMI_MAX_BPC..DSHAL_MAX_HDMI_MAX_BPC are clamped to that range.
 * @return 0 on success (at least one HDMI output updated), -1 on failure.
 */
int dsApplyHdmiMaxBpcRequestValue(int requestedMaxBpc)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    requestedMaxBpc = dsClampHdmiMaxBpc(requestedMaxBpc);
    hal_dbg("Applying max bpc=%d via DRM property on %s\n", requestedMaxBpc, cardPath);

    int drmFd = open(cardPath, O_RDWR);
    if (drmFd < 0) {
        hal_warn("Failed to open %s for DRM property write (%s)\n", cardPath, strerror(errno));
        return -1;
    }
    {
        int flags = fcntl(drmFd, F_GETFD);
        if (flags != -1) {
            (void)fcntl(drmFd, F_SETFD, flags | FD_CLOEXEC);
        }
    }

    drmModeRes *resources = drmModeGetResources(drmFd);
    if (!resources) {
        hal_warn("drmModeGetResources failed on %s (%s)\n", cardPath, strerror(errno));
        close(drmFd);
        return -1;
    }

    int appliedCount = 0;
    int foundCount = 0;

    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector = drmModeGetConnector(drmFd, resources->connectors[i]);
        if (!connector) {
            continue;
        }

        if (connector->connector_type != DRM_MODE_CONNECTOR_HDMIA
#ifdef DRM_MODE_CONNECTOR_HDMIB
            && connector->connector_type != DRM_MODE_CONNECTOR_HDMIB
#endif
           ) {
            drmModeFreeConnector(connector);
            continue;
        }

        if (connector->connection != DRM_MODE_CONNECTED) {
            drmModeFreeConnector(connector);
            continue;
        }

        foundCount++;

        /* Enumerate connector properties to find "max bpc" */
        drmModeObjectProperties *props = drmModeObjectGetProperties(drmFd,
                                             connector->connector_id,
                                             DRM_MODE_OBJECT_CONNECTOR);
        if (!props) {
            hal_warn("drmModeObjectGetProperties failed for connector %u (%s)\n",
                     connector->connector_id, strerror(errno));
            drmModeFreeConnector(connector);
            continue;
        }

        uint32_t maxBpcPropId = 0;
        for (uint32_t j = 0; j < props->count_props; j++) {
            drmModePropertyRes *prop = drmModeGetProperty(drmFd, props->props[j]);
            if (!prop) {
                continue;
            }
            if (strcmp(prop->name, "max bpc") == 0) {
                maxBpcPropId = prop->prop_id;
                drmModeFreeProperty(prop);
                break;
            }
            drmModeFreeProperty(prop);
        }
        drmModeFreeObjectProperties(props);

        if (maxBpcPropId == 0) {
            hal_warn("Connector %u has no 'max bpc' property\n", connector->connector_id);
            drmModeFreeConnector(connector);
            continue;
        }

        int ret = drmModeConnectorSetProperty(drmFd, connector->connector_id,
                                              maxBpcPropId, (uint64_t)requestedMaxBpc);
        if (ret == 0) {
            appliedCount++;
            hal_info("Applied max bpc=%d on HDMI connector %u\n",
                     requestedMaxBpc, connector->connector_id);
        } else {
            hal_warn("Failed to set max bpc=%d on connector %u (%s)\n",
                     requestedMaxBpc, connector->connector_id, strerror(errno));
        }

        drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);
    close(drmFd);

    if (foundCount == 0) {
        hal_warn("No connected HDMI connectors found on %s\n", cardPath);
    } else if (appliedCount == 0) {
        hal_warn("'max bpc' property not settable on %s (kernel may not expose it or DRM master required)\n", cardPath);
    }
    return (appliedCount > 0) ? 0 : -1;
}

/**
 * @brief Apply the requested maximum bits per color (bpc) for HDMI outputs via the DRM.
 * The requested max bpc is determined by the DSHAL_HDMI_MAX_BPC environment variable.
 * @return 0 on success (at least one HDMI output updated), -1 on failure (no outputs updated or error).
 */
int dsApplyHdmiMaxBpcRequest(void)
{
    return dsApplyHdmiMaxBpcRequestValue(dsGetRequestedHdmiMaxBpc());
}

/**
 * @brief Get the EDID bytes from the connected HDMI display.
 * @param[out] edid Buffer to store the retrieved EDID bytes.
 * @param[out] length Pointer to an integer to store the length of the retrieved EDID data.
 * @return 0 on success, -1 on failure.
 */
int dsInternalGetHdmiEdidBytes(unsigned char *edid, int *length)
{
    bool drmConnected = false, drmEnabled = false;
    char edidPath[PATH_MAX] = {0};
    char statusPath[PATH_MAX] = {0};
    char connectorName[64] = {0};
    char cardName[PATH_MAX] = {0};

    if (edid == NULL || length == NULL) {
        return -1;
    }

    if (!dsGetHdmiConnectorState(&drmConnected, &drmEnabled) || !drmConnected) {
        *length = 0;
        return -1;
    }
    (void)drmEnabled; /* EDID is readable from sysfs regardless of CRTC-enabled state */

    dsResolveDrmCardName(cardName, sizeof(cardName));
    DIR *drmClass = opendir("/sys/class/drm");
    if (!drmClass) {
        return -1;
    }

    struct dirent *entry;
    *length = 0;
    while ((entry = readdir(drmClass)) != NULL) {
        if (strncmp(entry->d_name, cardName, strlen(cardName)) != 0) {
            continue;
        }
        /* RPI4 in STB mode configured to enable/support only output through HDMI0*/
        if (strstr(entry->d_name, "HDMI-A-1") == NULL) {
            continue;
        }

        int statusLen = snprintf(statusPath, sizeof(statusPath), "/sys/class/drm/%s/status", entry->d_name);
        if (statusLen < 0 || (size_t)statusLen >= sizeof(statusPath)) {
            continue;
        }

        FILE *statusFile = fopen(statusPath, "r");
        if (statusFile == NULL) {
            continue;
        }

        char status[16] = {0};
        if (fgets(status, sizeof(status), statusFile) == NULL) {
            fclose(statusFile);
            continue;
        }
        fclose(statusFile);

        if (strncmp(status, "connected", strlen("connected")) != 0) {
            continue;
        }

        int pathLen = snprintf(edidPath, sizeof(edidPath), "/sys/class/drm/%s/edid", entry->d_name);
        if (pathLen < 0 || (size_t)pathLen >= sizeof(edidPath)) {
            continue;
        }

        FILE *edidFile = fopen(edidPath, "rb");
        if (!edidFile) {
            continue;
        }

        *length = (int)fread(edid, 1, DSHALUTILS_EDID_MAX_BYTES, edidFile);
        fclose(edidFile);

        if (*length <= 0) {
            closedir(drmClass);
            return -1;
        }

        strncpy(connectorName, entry->d_name, sizeof(connectorName) - 1);
        connectorName[sizeof(connectorName) - 1] = '\0';
        hal_dbg("Read %d bytes of EDID from %s(%s)\n", *length, edidPath, connectorName);
        break;
    }
    closedir(drmClass);

    if (*length == 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief Get the preferred HDMI mode.
 * @param[out] mode Buffer to store the preferred HDMI mode.
 * @param[in] len Length of the buffer.
 * @return true if a preferred HDMI mode was found, false otherwise.
 */
bool dsGetPreferredHdmiMode(char *mode, size_t len)
{
    int drmFd = -1;
    drmModeRes *resources = NULL;
    drmModeModeInfo selectedMode = {0};
    bool haveMode = false;
    bool selectedConnected = false;

    if (mode == NULL || len == 0) {
        return false;
    }

    mode[0] = '\0';

    drmFd = dsOpenDrmCardFd();
    if (drmFd < 0) {
        return false;
    }

    resources = drmModeGetResources(drmFd);
    if (!resources) {
        close(drmFd);
        return false;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector = drmModeGetConnectorCurrent(drmFd, resources->connectors[i]);
        if (!connector) {
            connector = drmModeGetConnector(drmFd, resources->connectors[i]);
        }
        if (!connector) {
            continue;
        }

        if (connector->connector_type != DRM_MODE_CONNECTOR_HDMIA
#ifdef DRM_MODE_CONNECTOR_HDMIB
            && connector->connector_type != DRM_MODE_CONNECTOR_HDMIB
#endif
        ) {
            drmModeFreeConnector(connector);
            continue;
        }

        if (connector->count_modes <= 0) {
            drmModeFreeConnector(connector);
            continue;
        }

        int preferredIndex = 0;
        for (int m = 0; m < connector->count_modes; m++) {
            if (connector->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                preferredIndex = m;
                break;
            }
        }

        bool entryConnected = (connector->connection == DRM_MODE_CONNECTED);

        if (entryConnected) {
            selectedMode = connector->modes[preferredIndex];
            haveMode = true;
            selectedConnected = true;
            drmModeFreeConnector(connector);
            break;
        }

        if (!haveMode) {
            selectedMode = connector->modes[preferredIndex];
            haveMode = true;
        }

        drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);
    close(drmFd);

    if (!haveMode) {
        return false;
    }

    snprintf(mode, len, "%s", selectedMode.name);
    if (!selectedConnected) {
        hal_dbg("No connected HDMI mode found; returning first available mode '%s'\n", mode);
    }

    return (mode[0] != '\0');
}

/**
 * @brief Map of HDMI resolutions to their corresponding CTA-861 VICs for enumeration based on EDID.
 * @reference This list is not exhaustive; it includes commonly used HDMI resolutions.  The parseHdmiResolutionsFromCtaDataBlock()
 *
 * IMPORTANT ORDERING: Implicit-rate entries (bare tokens like "480p", "720p", "1080p") MUST come
 * before their explicit-rate counterparts ("480p60", "720p60", "1080p60") to ensure dsgetResolutionInfo()
 * prefix-match fallback returns the correct default rate. If adding or removing entries, maintain this grouping.
 */
const hdmiSupportedRes_t resolutionMap[] = {
    {"480p", 2},       // 720x480p @ 59.94/60Hz  (CTA-861 VIC 2, rate-implicit alias)
    {"480p", 3},       // 720x480p @ 59.94/60Hz  (CTA-861 VIC 3, rate-implicit alias)
    {"480p60", 2},     // 720x480p @ 59.94/60Hz  (CTA-861 VIC 2, rate-explicit alias)
    {"480p60", 3},     // 720x480p @ 59.94/60Hz  (CTA-861 VIC 3, rate-explicit alias)
    {"480i", 6},       // 720x480i @ 59.94/60Hz  (CTA-861 VIC 6, rate-implicit alias)
    {"480i", 7},       // 720x480i @ 59.94/60Hz  (CTA-861 VIC 7, rate-implicit alias)
    {"480i60", 6},     // 720x480i @ 59.94/60Hz  (CTA-861 VIC 6, rate-explicit alias)
    {"480i60", 7},     // 720x480i @ 59.94/60Hz  (CTA-861 VIC 7, rate-explicit alias)
    {"576p50", 17},    // 720x576p @ 50Hz        (CTA-861 VIC 17)
    {"576p50", 18},    // 720x576p @ 50Hz        (CTA-861 VIC 18)
    {"576i50", 21},    // 720x576i @ 50Hz        (CTA-861 VIC 21)
    {"576i50", 22},    // 720x576i @ 50Hz        (CTA-861 VIC 22)
    {"720p", 4},       // 1280x720p @ 59.94/60Hz (CTA-861 VIC 4, rate-implicit alias)
    {"720p50", 19},    // 1280x720p @ 50Hz       (CTA-861 VIC 19)
    {"720p60", 4},     // 1280x720p @ 59.94/60Hz (CTA-861 VIC 4, rate-explicit alias)
    {"1080i", 5},      // 1920x1080i @ 59.94/60Hz (CTA-861 VIC 5, rate-implicit alias)
    {"1080i50", 20},   // 1920x1080i @ 50Hz      (CTA-861 VIC 20)
    {"1080i60", 5},    // 1920x1080i @ 59.94/60Hz (CTA-861 VIC 5, rate-explicit alias)
    {"1080p", 16},     // 1920x1080p @ 59.94/60Hz (CTA-861 VIC 16, rate-implicit alias)
    {"1080p24", 32},   // 1920x1080p @ 24Hz      (CTA-861 VIC 32)
    {"1080p25", 33},   // 1920x1080p @ 25Hz      (CTA-861 VIC 33)
    {"1080p30", 34},   // 1920x1080p @ 30Hz      (CTA-861 VIC 34)
    {"1080p50", 31},   // 1920x1080p @ 50Hz      (CTA-861 VIC 31)
    {"1080p60", 16},   // 1920x1080p @ 59.94/60Hz (CTA-861 VIC 16, rate-explicit alias)
    {"2160p24", 93},   // 3840x2160p @ 23.97/24Hz   (CTA-861 VIC 93,  16:9)
    {"2160p25", 94},   // 3840x2160p @ 25Hz         (CTA-861 VIC 94,  16:9)
    {"2160p30", 95},   // 3840x2160p @ 29.97/30Hz   (CTA-861 VIC 95,  16:9)
    {"2160p50", 96},   // 3840x2160p @ 50Hz         (CTA-861 VIC 96,  16:9)
    {"2160p60", 97},   // 3840x2160p @ 59.94/60Hz   (CTA-861 VIC 97,  16:9)
    {"2160p24", 98},   // 4096x2160p @ 23.97/24Hz   (CTA-861 VIC 98,  256:135)
    {"2160p25", 99},   // 4096x2160p @ 25Hz         (CTA-861 VIC 99,  256:135)
    {"2160p30", 100},  // 4096x2160p @ 29.97/30Hz   (CTA-861 VIC 100, 256:135)
    {"2160p50", 101},  // 4096x2160p @ 50Hz         (CTA-861 VIC 101, 256:135)
    {"2160p60", 102},  // 4096x2160p @ 59.94/60Hz   (CTA-861 VIC 102, 256:135)
    {"2160p24", 103},  // 3840x2160p @ 23.97/24Hz   (CTA-861 VIC 103, 64:27)
    {"2160p25", 104},  // 3840x2160p @ 25Hz         (CTA-861 VIC 104, 64:27)
    {"2160p30", 105},  // 3840x2160p @ 29.97/30Hz   (CTA-861 VIC 105, 64:27)
    {"2160p50", 106},  // 3840x2160p @ 50Hz         (CTA-861 VIC 106, 64:27)
    {"2160p60", 107},  // 3840x2160p @ 59.94/60Hz   (CTA-861 VIC 107, 64:27)
};

const size_t noOfItemsInResolutionMap = sizeof(resolutionMap) / sizeof(hdmiSupportedRes_t);

const VicMapEntry vicMapTable[] = {
    // 480i — VIC 6,7: 720x480i @ 59.94/60Hz (no 120/240Hz 480i VICs exist in CTA-861)
    {6,   dsTV_RESOLUTION_480i},    // 720x480i @ 59.94/60Hz (CTA-861 VIC 6,  4:3)
    {7,   dsTV_RESOLUTION_480i},    // 720x480i @ 59.94/60Hz (CTA-861 VIC 7,  16:9)

    // 480p — VIC 2,3: 59.94/60Hz; VIC 48,49: 119.88/120Hz; VIC 56,57: 239.76/240Hz
    {2,   dsTV_RESOLUTION_480p},    // 720x480p @ 59.94/60Hz  (CTA-861 VIC 2,  4:3)
    {3,   dsTV_RESOLUTION_480p},    // 720x480p @ 59.94/60Hz  (CTA-861 VIC 3,  16:9)
    {48,  dsTV_RESOLUTION_480p},    // 720x480p @ 119.88/120Hz (CTA-861 VIC 48, 4:3)
    {49,  dsTV_RESOLUTION_480p},    // 720x480p @ 119.88/120Hz (CTA-861 VIC 49, 16:9)
    {56,  dsTV_RESOLUTION_480p},    // 720x480p @ 239.76/240Hz (CTA-861 VIC 56, 4:3)
    {57,  dsTV_RESOLUTION_480p},    // 720x480p @ 239.76/240Hz (CTA-861 VIC 57, 16:9)

    // 576i — VIC 21,22: 50Hz only (no 100/200Hz 576i VICs in standard use)
    {21,  dsTV_RESOLUTION_576i},    // 720x576i @ 50Hz (CTA-861 VIC 21, 4:3)
    {22,  dsTV_RESOLUTION_576i},    // 720x576i @ 50Hz (CTA-861 VIC 22, 16:9)

    // 576p — VIC 17,18: 50Hz; VIC 42,43: 100Hz; VIC 52,53: 200Hz
    {17,  dsTV_RESOLUTION_576p50},  // 720x576p @ 50Hz  (CTA-861 VIC 17, 4:3)
    {18,  dsTV_RESOLUTION_576p50},  // 720x576p @ 50Hz  (CTA-861 VIC 18, 16:9)
    {42,  dsTV_RESOLUTION_576p},    // 720x576p @ 100Hz (CTA-861 VIC 42, 4:3)
    {43,  dsTV_RESOLUTION_576p},    // 720x576p @ 100Hz (CTA-861 VIC 43, 16:9)
    {52,  dsTV_RESOLUTION_576p},    // 720x576p @ 200Hz (CTA-861 VIC 52, 4:3)
    {53,  dsTV_RESOLUTION_576p},    // 720x576p @ 200Hz (CTA-861 VIC 53, 16:9)

    // 720p — VIC 4: 60Hz; VIC 19: 50Hz; VIC 41: 100Hz; VIC 47: 120Hz
    //        VIC 60-62: 24/25/30Hz; VIC 65-71: 64:27 wide variants
    {4,   dsTV_RESOLUTION_720p},    // 1280x720p @ 59.94/60Hz  (CTA-861 VIC 4)
    {19,  dsTV_RESOLUTION_720p50},  // 1280x720p @ 50Hz         (CTA-861 VIC 19)
    {41,  dsTV_RESOLUTION_720p},    // 1280x720p @ 100Hz        (CTA-861 VIC 41)
    {47,  dsTV_RESOLUTION_720p},    // 1280x720p @ 119.88/120Hz (CTA-861 VIC 47)
    {60,  dsTV_RESOLUTION_720p},    // 1280x720p @ 23.97/24Hz   (CTA-861 VIC 60)
    {61,  dsTV_RESOLUTION_720p},    // 1280x720p @ 25Hz         (CTA-861 VIC 61)
    {62,  dsTV_RESOLUTION_720p},    // 1280x720p @ 29.97/30Hz   (CTA-861 VIC 62)
    {65,  dsTV_RESOLUTION_720p},    // 1280x720p @ 23.97/24Hz   (CTA-861 VIC 65, 64:27)
    {66,  dsTV_RESOLUTION_720p},    // 1280x720p @ 25Hz         (CTA-861 VIC 66, 64:27)
    {67,  dsTV_RESOLUTION_720p},    // 1280x720p @ 29.97/30Hz   (CTA-861 VIC 67, 64:27)
    {68,  dsTV_RESOLUTION_720p50},  // 1280x720p @ 50Hz         (CTA-861 VIC 68, 64:27)
    {69,  dsTV_RESOLUTION_720p},    // 1280x720p @ 59.94/60Hz   (CTA-861 VIC 69, 64:27)
    {70,  dsTV_RESOLUTION_720p},    // 1280x720p @ 100Hz        (CTA-861 VIC 70, 64:27)
    {71,  dsTV_RESOLUTION_720p},    // 1280x720p @ 119.88/120Hz (CTA-861 VIC 71, 64:27)

    // 1080i — VIC 5: 60Hz; VIC 20: 50Hz; VIC 39: 1250-line 50Hz; VIC 40: 100Hz; VIC 46: 120Hz
    {5,   dsTV_RESOLUTION_1080i},   // 1920x1080i @ 59.94/60Hz        (CTA-861 VIC 5)
    {20,  dsTV_RESOLUTION_1080i50}, // 1920x1080i @ 50Hz              (CTA-861 VIC 20)
    {39,  dsTV_RESOLUTION_1080i50}, // 1920x1080i (1250-line) @ 50Hz  (CTA-861 VIC 39)
    {40,  dsTV_RESOLUTION_1080i},   // 1920x1080i @ 100Hz             (CTA-861 VIC 40)
    {46,  dsTV_RESOLUTION_1080i},   // 1920x1080i @ 119.88/120Hz      (CTA-861 VIC 46)

    // 1080p — VIC 16: 60Hz; VIC 31: 50Hz; VIC 32-34: 24/25/30Hz
    //         VIC 63: 120Hz; VIC 64: 100Hz; VIC 72-78: 64:27 wide variants
    {16,  dsTV_RESOLUTION_1080p60}, // 1920x1080p @ 59.94/60Hz  (CTA-861 VIC 16)
    {31,  dsTV_RESOLUTION_1080p50}, // 1920x1080p @ 50Hz         (CTA-861 VIC 31)
    {32,  dsTV_RESOLUTION_1080p24}, // 1920x1080p @ 23.97/24Hz   (CTA-861 VIC 32)
    {33,  dsTV_RESOLUTION_1080p25}, // 1920x1080p @ 25Hz         (CTA-861 VIC 33)
    {34,  dsTV_RESOLUTION_1080p30}, // 1920x1080p @ 29.97/30Hz   (CTA-861 VIC 34)
    {63,  dsTV_RESOLUTION_1080p},   // 1920x1080p @ 119.88/120Hz (CTA-861 VIC 63)
    {64,  dsTV_RESOLUTION_1080p},   // 1920x1080p @ 100Hz        (CTA-861 VIC 64)
    {72,  dsTV_RESOLUTION_1080p24}, // 1920x1080p @ 23.97/24Hz   (CTA-861 VIC 72, 64:27)
    {73,  dsTV_RESOLUTION_1080p25}, // 1920x1080p @ 25Hz         (CTA-861 VIC 73, 64:27)
    {74,  dsTV_RESOLUTION_1080p30}, // 1920x1080p @ 29.97/30Hz   (CTA-861 VIC 74, 64:27)
    {75,  dsTV_RESOLUTION_1080p50}, // 1920x1080p @ 50Hz         (CTA-861 VIC 75, 64:27)
    {76,  dsTV_RESOLUTION_1080p60}, // 1920x1080p @ 59.94/60Hz   (CTA-861 VIC 76, 64:27)
    {77,  dsTV_RESOLUTION_1080p},   // 1920x1080p @ 100Hz        (CTA-861 VIC 77, 64:27)
    {78,  dsTV_RESOLUTION_1080p},   // 1920x1080p @ 119.88/120Hz (CTA-861 VIC 78, 64:27)

    // 3840x2160p (UHD-1) — VIC 93-97: 16:9; VIC 103-107: 64:27
    {93,  dsTV_RESOLUTION_2160p24}, // 3840x2160p @ 23.97/24Hz   (CTA-861 VIC 93,  16:9)
    {94,  dsTV_RESOLUTION_2160p25}, // 3840x2160p @ 25Hz         (CTA-861 VIC 94,  16:9)
    {95,  dsTV_RESOLUTION_2160p30}, // 3840x2160p @ 29.97/30Hz   (CTA-861 VIC 95,  16:9)
    {96,  dsTV_RESOLUTION_2160p50}, // 3840x2160p @ 50Hz         (CTA-861 VIC 96,  16:9)
    {97,  dsTV_RESOLUTION_2160p60}, // 3840x2160p @ 59.94/60Hz   (CTA-861 VIC 97,  16:9)
    {103, dsTV_RESOLUTION_2160p24}, // 3840x2160p @ 23.97/24Hz   (CTA-861 VIC 103, 64:27)
    {104, dsTV_RESOLUTION_2160p25}, // 3840x2160p @ 25Hz         (CTA-861 VIC 104, 64:27)
    {105, dsTV_RESOLUTION_2160p30}, // 3840x2160p @ 29.97/30Hz   (CTA-861 VIC 105, 64:27)
    {106, dsTV_RESOLUTION_2160p50}, // 3840x2160p @ 50Hz         (CTA-861 VIC 106, 64:27)
    {107, dsTV_RESOLUTION_2160p60}, // 3840x2160p @ 59.94/60Hz   (CTA-861 VIC 107, 64:27)

    // 4096x2160p (DCI 4K) — VIC 98-102: 256:135
    {98,  dsTV_RESOLUTION_2160p24}, // 4096x2160p @ 23.97/24Hz   (CTA-861 VIC 98,  256:135)
    {99,  dsTV_RESOLUTION_2160p25}, // 4096x2160p @ 25Hz         (CTA-861 VIC 99,  256:135)
    {100, dsTV_RESOLUTION_2160p30}, // 4096x2160p @ 29.97/30Hz   (CTA-861 VIC 100, 256:135)
    {101, dsTV_RESOLUTION_2160p50}, // 4096x2160p @ 50Hz         (CTA-861 VIC 101, 256:135)
    {102, dsTV_RESOLUTION_2160p60}, // 4096x2160p @ 59.94/60Hz   (CTA-861 VIC 102, 256:135)
};

#define VIC_MAP_TABLE_SIZE (sizeof(vicMapTable) / sizeof(VicMapEntry))

static int detailedBlock(unsigned char *x, int extension, dsDisplayEDID_t *displayEdidInfo)
{
    hal_info("extension %d\n", extension);
    static unsigned char name[53];
    switch (x[3]) {
        case 0xFC:
            if (strchr((char *)name, '\n'))
                return 1;
            strncat((char *)name, (char *)x + 5, 13);
            strncpy(displayEdidInfo->monitorName, (const char *)name, dsEEDID_MAX_MON_NAME_LENGTH);
            return 1;
        default:
            return 1;
    }
}

static void hdmi_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    displayEdidInfo->physicalAddressA = (x[4] >> 4);
    displayEdidInfo->physicalAddressB = (x[4] & 0x0f);
    displayEdidInfo->physicalAddressC = (x[5] >> 4);
    displayEdidInfo->physicalAddressD = (x[5] & 0x0f);
    if (displayEdidInfo->physicalAddressB)
        displayEdidInfo->isRepeater = true;
    else
        displayEdidInfo->isRepeater = false;
}

static void cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    unsigned int oui;
    switch ((x[0] & 0xe0) >> 5) {
        case 0x03:
            oui = (x[3] << 16) + (x[2] << 8) + x[1];
            if (oui == 0x000c03) {
                hdmi_cea_block(x, displayEdidInfo);
                displayEdidInfo->hdmiDeviceType = true;
            }
            break;
        default:
            break;
    }
}

static int parse_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    int ret = 0;
    int version = x[1];
    int offset = x[2];
    unsigned char *detailed_buf;
    if (version == 3) {
        for (int i = 4; i < offset; i += (x[i] & 0x1f) + 1) {
            cea_block(x + i, displayEdidInfo);
        }
    }
    for (detailed_buf = x + offset; detailed_buf + 18 < x + 127; detailed_buf += 18)
        if (detailed_buf[0])
            detailedBlock(detailed_buf, 1, displayEdidInfo);
    return ret;
}

int fill_edid_struct(unsigned char *edidBytes, dsDisplayEDID_t *displayEdidInfo, int size)
{
    hal_info("invoked.\n");
    unsigned char *x;
    time_t t;
    struct tm *localtm;
    int i;
    if (!edidBytes || memcmp(edidBytes, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00", 8)) {
        hal_dbg("Header not found\n");
        return -1;
    }
    displayEdidInfo->productCode = edidBytes[0x0A] + (edidBytes[0x0B] << 8);
    displayEdidInfo->serialNumber = (edidBytes[0x0C] + (edidBytes[0x0D] << 8) + (edidBytes[0x0E] << 16) + (edidBytes[0x0F] << 24));
    displayEdidInfo->hdmiDeviceType = true; // This is true for Rpi
    time(&t);
    localtm = localtime(&t);
    if (edidBytes[0x10] < 55 || edidBytes[0x10] == 0xff)
    {
        if (edidBytes[0x11] > 0x0f)
        {
            if (edidBytes[0x10] == 0xff)
            {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11];
            }
            else if (edidBytes[0x11] + 90 <= localtm->tm_year)
            {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11] + 1990;
            }
        }
    }
    detailedBlock(edidBytes + 0x36, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x48, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x5A, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x6C, 0, displayEdidInfo);
    x = edidBytes;
    for (i = 128; i < size; i += 128) {
        if (x[0] == 0x02)
            parse_cea_block(x, displayEdidInfo);
    }
    return 0;
}

void parse_edid(const uint8_t *edid, EDID_t *parsed_edid)
{
    memcpy(parsed_edid->header, edid, 8);
    parsed_edid->manufacturer_id = (edid[8] << 8) | edid[9];
    parsed_edid->product_code = (edid[10] << 8) | edid[11];
    parsed_edid->serial_number = (edid[12] << 24) | (edid[13] << 16) | (edid[14] << 8) | edid[15];
    parsed_edid->week_of_manufacture = edid[16];
    parsed_edid->year_of_manufacture = edid[17] + 1990;
    parsed_edid->edid_version = edid[18];
    parsed_edid->edid_revision = edid[19];
    memcpy(parsed_edid->basic_display_params, &edid[20], 5);
    memcpy(parsed_edid->chromaticity_coords, &edid[25], 10);
    memcpy(parsed_edid->established_timings, &edid[35], 3);
    memcpy(parsed_edid->standard_timings, &edid[38], 16);
    memcpy(parsed_edid->detailed_timing_descriptors, &edid[54], 72);
    parsed_edid->extension_flag = edid[126];
    parsed_edid->checksum = edid[127];
}

void print_edid(const EDID_t *parsed_edid)
{
    printf("Header: ");
    for (int i = 0; i < 8; i++) {
        printf("%02x ", parsed_edid->header[i]);
    }
    printf("\n");

    printf("Manufacturer ID: %04x\n", parsed_edid->manufacturer_id);
    printf("Product Code: %04x\n", parsed_edid->product_code);
    printf("Serial Number: %08x\n", parsed_edid->serial_number);
    printf("Week of Manufacture: %d\n", parsed_edid->week_of_manufacture);
    printf("Year of Manufacture: %d\n", parsed_edid->year_of_manufacture);
    printf("EDID Version: %d\n", parsed_edid->edid_version);
    printf("EDID Revision: %d\n", parsed_edid->edid_revision);

    printf("Basic Display Parameters: ");
    for (int i = 0; i < 5; i++) {
        printf("%02x ", parsed_edid->basic_display_params[i]);
    }
    printf("\n");

    printf("Chromaticity Coordinates: ");
    for (int i = 0; i < 10; i++) {
        printf("%02x ", parsed_edid->chromaticity_coords[i]);
    }
    printf("\n");

    printf("Established Timings: ");
    for (int i = 0; i < 3; i++) {
        printf("%02x ", parsed_edid->established_timings[i]);
    }
    printf("\n");

    printf("Standard Timings: ");
    for (int i = 0; i < 16; i++) {
        printf("%02x ", parsed_edid->standard_timings[i]);
    }
    printf("\n");

    printf("Detailed Timing Descriptors: ");
    for (int i = 0; i < 72; i++) {
        printf("%02x ", parsed_edid->detailed_timing_descriptors[i]);
    }
    printf("\n");

    printf("Extension Flag: %02x\n", parsed_edid->extension_flag);
    printf("Checksum: %02x\n", parsed_edid->checksum);
}

/**
 * @brief Get the value of XDG_RUNTIME_DIR from the westeros environment file
 * @return A pointer to the cached value of XDG_RUNTIME_DIR, or NULL if not found.
 */
const char *getXDGRuntimeDir()
{
    static char cachedValue[PATH_MAX] = {0};
    static bool isCached = false;

    if (isCached) {
        return cachedValue;
    }

    if (access(WESTEROS_ENV_FILE, F_OK) == -1) {
        hal_err("File '%s' not found\n", WESTEROS_ENV_FILE);
        return NULL;
    }

    FILE *file = fopen(WESTEROS_ENV_FILE, "r");
    if (file == NULL) {
        hal_err("Failed to open file '%s'\n", WESTEROS_ENV_FILE);
        return NULL;
    }

    char line[PATH_MAX] = {0};
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "XDG_RUNTIME_DIR=", strlen("XDG_RUNTIME_DIR=")) == 0) {
            size_t len = strcspn(line + strlen("XDG_RUNTIME_DIR="), "\r\n");
            if (len >= sizeof(cachedValue)) {
                hal_err("XDG_RUNTIME_DIR value is too long\n");
                fclose(file);
                return NULL;
            }

            strncpy(cachedValue, line + strlen("XDG_RUNTIME_DIR="), len);
            cachedValue[len] = '\0';
            isCached = true;

            hal_dbg("XDG_RUNTIME_DIR from '%s': '%s'\n", WESTEROS_ENV_FILE, cachedValue);
            fclose(file);
            return cachedValue;
        }
    }

    hal_err("XDG_RUNTIME_DIR not found in '%s'\n", WESTEROS_ENV_FILE);
    fclose(file);
    return NULL;
}

/**
 * @brief Send a display command to the westeros display socket and receive the response.
 * @param cmd The display command to send (for example: 'set display enable 1').
 *                 set display enable 1/0
 *                 get mode
 *                 set mode 1920x1080p25
 * @param resp Buffer to receive the response from the display socket.
 * @param respSize Size of the response buffer.
 * @return true if the command was sent and a response was received successfully, false otherwise.
 */
bool westerosGLConsoleRWWrapper(const char *cmd, char *resp, size_t respSize)
{
    if (cmd == NULL || resp == NULL || respSize == 0) {
        return false;
    }

    resp[0] = '\0';

    const char *displayCmd = cmd;
    while (*displayCmd == ' ' || *displayCmd == '\t') {
        ++displayCmd;
    }

    if (strstr(displayCmd, "westeros-gl-console") != NULL || strchr(displayCmd, ';') != NULL) {
        hal_err("westerosGLConsoleRWWrapper expects bare display command (for example: 'set display enable 1')\n");
        return false;
    }

    if (*displayCmd == '\0') {
        hal_err("Missing westeros display command in '%s'\n", cmd);
        return false;
    }

    const char *xdgRuntimeDir = (getXDGRuntimeDir() != NULL) ? getXDGRuntimeDir() : getenv("XDG_RUNTIME_DIR");
    if (xdgRuntimeDir == NULL || xdgRuntimeDir[0] == '\0') {
        hal_err("XDG_RUNTIME_DIR is not set for westeros command\n");
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_LOCAL;

    int pathLen = snprintf(addr.sun_path, sizeof(addr.sun_path), "%s/%s", xdgRuntimeDir, "display");
    if (pathLen <= 0 || pathLen >= (int)sizeof(addr.sun_path)) {
        hal_err("Display socket path is invalid for XDG_RUNTIME_DIR='%s'\n", xdgRuntimeDir);
        return false;
    }

    int socketFd = socket(PF_LOCAL, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socketFd < 0) {
        hal_err("Unable to open display socket: errno %d\n", errno);
        return false;
    }

    int addressSize = (int)offsetof(struct sockaddr_un, sun_path) + pathLen + 1;
    if (connect(socketFd, (struct sockaddr *)&addr, addressSize) < 0) {
        hal_err("Unable to connect to display socket '%s': errno %d\n", addr.sun_path, errno);
        close(socketFd);
        return false;
    }

    unsigned char tx[PATH_MAX];
    size_t displayCmdLen = strlen(displayCmd);
    size_t payloadLen = displayCmdLen + 1; /* Include terminating NUL per protocol framing. */
    const size_t maxProtocolPayloadLen = 254; /* One-byte length field; reserve payload to <= 254 bytes. */

    if (payloadLen > maxProtocolPayloadLen) {
        hal_err("Westeros command is too long for protocol framing (%zu > %zu payload bytes)\n",
                payloadLen, maxProtocolPayloadLen);
        close(socketFd);
        return false;
    }
    if (payloadLen > sizeof(tx) - 3) {
        hal_err("Westeros command payload does not fit tx buffer (%zu bytes)\n", payloadLen);
        close(socketFd);
        return false;
    }

    size_t txLen = 0;
    tx[txLen++] = 'D';
    tx[txLen++] = 'S';
    tx[txLen++] = (unsigned char)payloadLen;
    memcpy(&tx[txLen], displayCmd, payloadLen);
    txLen += payloadLen;

    size_t sentTotal = 0;
    while (sentTotal < txLen) {
        struct iovec txIov;
        txIov.iov_base = (char *)&tx[sentTotal];
        txIov.iov_len = txLen - sentTotal;

        struct msghdr txMsg;
        memset(&txMsg, 0, sizeof(txMsg));
        txMsg.msg_iov = &txIov;
        txMsg.msg_iovlen = 1;

        ssize_t sentLen;
        do {
            sentLen = sendmsg(socketFd, &txMsg, MSG_NOSIGNAL);
        } while (sentLen < 0 && errno == EINTR);

        if (sentLen <= 0) {
            hal_err("Failed to send display command '%s'\n", displayCmd);
            close(socketFd);
            return false;
        }

        sentTotal += (size_t)sentLen;
    }

    unsigned char rx[PATH_MAX] = {0};
    size_t recvTotal = 0;
    while (recvTotal < sizeof(rx)) {
        ssize_t recvLen;
        do {
            recvLen = recv(socketFd, &rx[recvTotal], sizeof(rx) - recvTotal, 0);
        } while (recvLen < 0 && errno == EINTR);

        if (recvLen < 0) {
            close(socketFd);
            hal_err("Failed to receive display response for '%s'\n", displayCmd);
            return false;
        }
        if (recvLen == 0) {
            break;
        }

        recvTotal += (size_t)recvLen;

        if (recvTotal >= 3) {
            if (rx[0] != 'D' || rx[1] != 'S') {
                break;
            }

            size_t frameLen = (size_t)rx[2] + 3;
            if (frameLen > sizeof(rx)) {
                break;
            }
            if (recvTotal >= frameLen) {
                break;
            }
        }
    }

    close(socketFd);

    if (recvTotal == 0) {
        hal_err("Failed to receive display response for '%s'\n", displayCmd);
        return false;
    }

    unsigned char *m = rx;
    size_t remaining = recvTotal;
    while (remaining >= 4) {
        if (m[0] != 'D' || m[1] != 'S') {
            break;
        }

        int msgLen = m[2];
        if (remaining < (size_t)(msgLen + 3)) {
            break;
        }

        const char *payload = (const char *)&m[3];
        const char *nulTerminator = memchr(payload, '\0', (size_t)msgLen);
        size_t copyLen = nulTerminator ? (size_t)(nulTerminator - payload) : (size_t)msgLen;
        if (copyLen >= respSize) {
            copyLen = respSize - 1;
        }
        memcpy(resp, payload, copyLen);
        resp[copyLen] = '\0';

        return true;
    }

    hal_err("Invalid display response framing for '%s'\n", displayCmd);
    return false;
}

const dsTVResolution_t *getResolutionFromVic(int vic)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].vic == vic) {
            return &vicMapTable[i].tvresolution;
        }
    }
    return NULL; // VIC not found
}

const int *getVicFromResolution(dsTVResolution_t resolution)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].tvresolution == resolution) {
            return &vicMapTable[i].vic;
        }
    }
    return NULL;  // VIC not found
}
