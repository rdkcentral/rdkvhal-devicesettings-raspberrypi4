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
 */

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "dshalUtils.h"
#include "dshalLogger.h"

int dsOpenDrmCardFd(void)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    int fd = open(cardPath, O_RDWR);
    if (fd < 0) {
        fd = open(cardPath, O_RDONLY);
    }

    if (fd >= 0) {
        int flags = fcntl(fd, F_GETFD);
        if (flags != -1) {
            (void)fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
        }
    }

    return fd;
}

bool dsGetHdmiConnectorState(bool *connected, bool *enabled)
{
    int drmFd = -1;
    drmModeRes *resources = NULL;
    bool foundConnector = false;
    bool bestConnected = false;
    bool bestEnabled = false;

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

        if (entryConnected && entryEnabled) {
            bestConnected = true;
            bestEnabled = true;
            foundConnector = true;
            drmModeFreeConnector(connector);
            break;
        }

        if (!foundConnector) {
            bestConnected = entryConnected;
            bestEnabled = entryEnabled;
        }

        foundConnector = true;
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

const hdmiSupportedRes_t resolutionMap[] = {
    {"480p", 2},       // 720x480p @ 59.94/60Hz
    {"480p", 3},       // 720x480p @ 59.94/60Hz
    {"480i", 6},       // 720x480i @ 59.94/60Hz
    {"480i", 7},       // 720x480i @ 59.94/60Hz
    {"576p50", 17},      // 720x576p @ 50Hz
    {"576p50", 18},      // 720x576p @ 50Hz
    {"576i50", 21},      // 720x576i @ 50Hz
    {"576i50", 22},      // 720x576i @ 50Hz
    {"720p", 4},       // 1280x720p @ 59.94/60Hz
    {"720p50", 19},    // 1280x720p @ 50Hz
    {"1080i", 5},      // 1920x1080i @ 59.94/60Hz
    {"1080i50", 20},   // 1920x1080i @ 50Hz
    {"1080p24", 32},   // 1920x1080p @ 24Hz
    {"1080p25", 33},   // 1920x1080p @ 25Hz
    {"1080p30", 34},   // 1920x1080p @ 30Hz
    {"1080p50", 31},   // 1920x1080p @ 50Hz
    {"1080p60", 16},   // 1920x1080p @ 59.94/60Hz
    {"2160p24", 93},   // 3840x2160p @ 24Hz
    {"2160p25", 94},   // 3840x2160p @ 25Hz
    {"2160p30", 95},   // 3840x2160p @ 30Hz
    {"2160p24", 98},   // 4096x2160p @ 24Hz
    {"2160p25", 99},   // 4096x2160p @ 25Hz
    {"2160p30", 100},  // 4096x2160p @ 30Hz
    {"2160p50", 101},  // 4096x2160p @ 50Hz
    {"2160p60", 102}   // 4096x2160p @ 60Hz
};

const size_t noOfItemsInResolutionMap = sizeof(resolutionMap) / sizeof(hdmiSupportedRes_t);

const VicMapEntry vicMapTable[] = {
    // 480i resolutions
    {6, dsTV_RESOLUTION_480i},    // 720x480i @ 59.94/60Hz
    {7, dsTV_RESOLUTION_480i},    // 720x480i @ 59.94/60Hz
    {48, dsTV_RESOLUTION_480i},   // 720x480i @ 120Hz
    {49, dsTV_RESOLUTION_480i},   // 720x480i @ 120Hz
    {56, dsTV_RESOLUTION_480i},   // 720x480i @ 240Hz
    {57, dsTV_RESOLUTION_480i},   // 720x480i @ 240Hz

    // 480p resolutions
    {2, dsTV_RESOLUTION_480p},    // 720x480p @ 59.94/60Hz
    {3, dsTV_RESOLUTION_480p},    // 720x480p @ 59.94/60Hz
    {46, dsTV_RESOLUTION_480p},   // 720x480p @ 120Hz
    {47, dsTV_RESOLUTION_480p},   // 720x480p @ 120Hz
    {54, dsTV_RESOLUTION_480p},   // 720x480p @ 240Hz
    {55, dsTV_RESOLUTION_480p},   // 720x480p @ 240Hz

    // 576i resolutions
    {21, dsTV_RESOLUTION_576i},   // 720x576i @ 50Hz
    {22, dsTV_RESOLUTION_576i},   // 720x576i @ 50Hz
    {42, dsTV_RESOLUTION_576i},   // 720x576i @ 100Hz
    {43, dsTV_RESOLUTION_576i},   // 720x576i @ 100Hz
    {52, dsTV_RESOLUTION_576i},   // 720x576i @ 200Hz
    {53, dsTV_RESOLUTION_576i},   // 720x576i @ 200Hz

    // 576p resolutions
    {17, dsTV_RESOLUTION_576p},   // 720x576p @ 50Hz
    {18, dsTV_RESOLUTION_576p},   // 720x576p @ 50Hz
    {40, dsTV_RESOLUTION_576p},   // 720x576p @ 100Hz
    {41, dsTV_RESOLUTION_576p},   // 720x576p @ 100Hz
    {50, dsTV_RESOLUTION_576p},   // 720x576p @ 200Hz
    {51, dsTV_RESOLUTION_576p},   // 720x576p @ 200Hz

    // 720p resolutions
    {4, dsTV_RESOLUTION_720p},    // 1280x720p @ 59.94/60Hz
    {19, dsTV_RESOLUTION_720p50}, // 1280x720p @ 50Hz
    {39, dsTV_RESOLUTION_720p},   // 1280x720p @ 100Hz
    {45, dsTV_RESOLUTION_720p},   // 1280x720p @ 120Hz
    {58, dsTV_RESOLUTION_720p},   // 1280x720p @ 24Hz
    {59, dsTV_RESOLUTION_720p},   // 1280x720p @ 25Hz
    {60, dsTV_RESOLUTION_720p},   // 1280x720p @ 30Hz
    {63, dsTV_RESOLUTION_720p},   // 1280x720p @ 24Hz
    {64, dsTV_RESOLUTION_720p},   // 1280x720p @ 25Hz
    {65, dsTV_RESOLUTION_720p},   // 1280x720p @ 30Hz
    {66, dsTV_RESOLUTION_720p50}, // 1280x720p @ 50Hz
    {67, dsTV_RESOLUTION_720p},   // 1280x720p @ 60Hz
    {68, dsTV_RESOLUTION_720p},   // 1280x720p @ 100Hz
    {69, dsTV_RESOLUTION_720p},   // 1280x720p @ 120Hz

    // 1080i resolutions
    {5, dsTV_RESOLUTION_1080i},   // 1920x1080i @ 59.94/60Hz
    {20, dsTV_RESOLUTION_1080i50},// 1920x1080i @ 50Hz
    {37, dsTV_RESOLUTION_1080i50},// 1920x1080i @ 50Hz
    {38, dsTV_RESOLUTION_1080i},  // 1920x1080i @ 100Hz
    {44, dsTV_RESOLUTION_1080i},  // 1920x1080i @ 120Hz

    // 1080p resolutions
    {16, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 59.94/60Hz
    {31, dsTV_RESOLUTION_1080p50},// 1920x1080p @ 50Hz
    {32, dsTV_RESOLUTION_1080p24},// 1920x1080p @ 24Hz
    {33, dsTV_RESOLUTION_1080p25},// 1920x1080p @ 25Hz
    {34, dsTV_RESOLUTION_1080p30},// 1920x1080p @ 30Hz
    {44, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 120Hz
    {61, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 120Hz
    {62, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 100Hz
    {70, dsTV_RESOLUTION_1080p24},// 1920x1080p @ 24Hz
    {71, dsTV_RESOLUTION_1080p25},// 1920x1080p @ 25Hz
    {72, dsTV_RESOLUTION_1080p30},// 1920x1080p @ 30Hz
    {73, dsTV_RESOLUTION_1080p50},// 1920x1080p @ 50Hz
    {74, dsTV_RESOLUTION_1080p60},// 1920x1080p @ 60Hz
    {75, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 100Hz
    {76, dsTV_RESOLUTION_1080p},  // 1920x1080p @ 120Hz

    // 2160p resolutions
    {77, dsTV_RESOLUTION_2160p24},// 3840x2160p @ 24Hz
    {78, dsTV_RESOLUTION_2160p25},// 3840x2160p @ 25Hz
    {79, dsTV_RESOLUTION_2160p30},// 3840x2160p @ 30Hz
    {80, dsTV_RESOLUTION_2160p50},// 3840x2160p @ 50Hz
    {81, dsTV_RESOLUTION_2160p60},// 3840x2160p @ 60Hz
    {87, dsTV_RESOLUTION_2160p24},// 3840x2160p @ 24Hz
    {88, dsTV_RESOLUTION_2160p25},// 3840x2160p @ 25Hz
    {89, dsTV_RESOLUTION_2160p30},// 3840x2160p @ 30Hz
    {90, dsTV_RESOLUTION_2160p50},// 3840x2160p @ 50Hz
    {91, dsTV_RESOLUTION_2160p60},// 3840x2160p @ 60Hz

    // 4K resolutions
    {82, dsTV_RESOLUTION_2160p24},// 4096x2160p @ 24Hz
    {83, dsTV_RESOLUTION_2160p25},// 4096x2160p @ 25Hz
    {84, dsTV_RESOLUTION_2160p30},// 4096x2160p @ 30Hz
    {85, dsTV_RESOLUTION_2160p50},// 4096x2160p @ 50Hz
    {86, dsTV_RESOLUTION_2160p60},// 4096x2160p @ 60Hz
    {93, dsTV_RESOLUTION_2160p24},// 4096x2160p @ 24Hz
    {94, dsTV_RESOLUTION_2160p25},// 4096x2160p @ 25Hz
    {95, dsTV_RESOLUTION_2160p30},// 4096x2160p @ 30Hz
    {98, dsTV_RESOLUTION_2160p24},// 4096x2160p @ 24Hz
    {99, dsTV_RESOLUTION_2160p25},// 4096x2160p @ 25Hz
    {100, dsTV_RESOLUTION_2160p30},// 4096x2160p @ 30Hz
    {101, dsTV_RESOLUTION_2160p50},// 4096x2160p @ 50Hz
    {102, dsTV_RESOLUTION_2160p60},// 4096x2160p @ 60Hz
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

    int addressSize = pathLen + (int)offsetof(struct sockaddr_un, sun_path);
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

    struct iovec txIov;
    txIov.iov_base = (char *)tx;
    txIov.iov_len = txLen;

    struct msghdr txMsg;
    memset(&txMsg, 0, sizeof(txMsg));
    txMsg.msg_iov = &txIov;
    txMsg.msg_iovlen = 1;

    ssize_t sentLen;
    do {
        sentLen = sendmsg(socketFd, &txMsg, MSG_NOSIGNAL);
    } while (sentLen < 0 && errno == EINTR);

    if (sentLen != (ssize_t)txLen) {
        hal_err("Failed to send display command '%s'\n", displayCmd);
        close(socketFd);
        return false;
    }

    unsigned char rx[PATH_MAX] = {0};
    struct iovec rxIov;
    rxIov.iov_base = (char *)rx;
    rxIov.iov_len = sizeof(rx);

    struct msghdr rxMsg;
    memset(&rxMsg, 0, sizeof(rxMsg));
    rxMsg.msg_iov = &rxIov;
    rxMsg.msg_iovlen = 1;

    ssize_t recvLen;
    do {
        recvLen = recvmsg(socketFd, &rxMsg, 0);
    } while (recvLen < 0 && errno == EINTR);

    close(socketFd);

    if (recvLen <= 0) {
        hal_err("Failed to receive display response for '%s'\n", displayCmd);
        return false;
    }

    unsigned char *m = rx;
    size_t remaining = (size_t)recvLen;
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
