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

#include "dshalUtils.h"
#include "dshalLogger.h"

const VicMapEntry vicMapTable[] = {
    // 480p resolutions
    {2, dsVIDEO_PIXELRES_720x480},    // 720x480p @ 59.94/60Hz
    {3, dsVIDEO_PIXELRES_720x480},    // 720x480p @ 59.94/60Hz
    {6, dsVIDEO_PIXELRES_720x480},    // 720x480i @ 59.94/60Hz
    {7, dsVIDEO_PIXELRES_720x480},    // 720x480i @ 59.94/60Hz
//    {8, dsVIDEO_PIXELRES_720x240},    // 720x240p @ 59.94/60Hz
//    {9, dsVIDEO_PIXELRES_720x240},    // 720x240p @ 59.94/60Hz
//    {10, dsVIDEO_PIXELRES_2880x480},  // 2880x480i @ 59.94/60Hz
//    {11, dsVIDEO_PIXELRES_2880x480},  // 2880x480i @ 59.94/60Hz
//    {12, dsVIDEO_PIXELRES_2880x240},  // 2880x240p @ 59.94/60Hz
//    {13, dsVIDEO_PIXELRES_2880x240},  // 2880x240p @ 59.94/60Hz
//    {14, dsVIDEO_PIXELRES_1440x480},  // 1440x480p @ 59.94/60Hz
//    {15, dsVIDEO_PIXELRES_1440x480},  // 1440x480p @ 59.94/60Hz
//    {35, dsVIDEO_PIXELRES_2880x480},  // 2880x480p @ 59.94/60Hz
    {46, dsVIDEO_PIXELRES_720x480},   // 720x480p @ 120Hz
    {47, dsVIDEO_PIXELRES_720x480},   // 720x480p @ 120Hz
    {48, dsVIDEO_PIXELRES_720x480},   // 720x480i @ 120Hz
    {49, dsVIDEO_PIXELRES_720x480},   // 720x480i @ 120Hz
    {54, dsVIDEO_PIXELRES_720x480},   // 720x480p @ 240Hz
    {55, dsVIDEO_PIXELRES_720x480},   // 720x480p @ 240Hz
    {56, dsVIDEO_PIXELRES_720x480},   // 720x480i @ 240Hz
    {57, dsVIDEO_PIXELRES_720x480},   // 720x480i @ 240Hz

    // 576p resolutions
    {17, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 50Hz
    {18, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 50Hz
    {21, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 50Hz
    {22, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 50Hz
//    {23, dsVIDEO_PIXELRES_720x288},   // 720x288p @ 50Hz
//    {24, dsVIDEO_PIXELRES_720x288},   // 720x288p @ 50Hz
//    {25, dsVIDEO_PIXELRES_2880x576},  // 2880x576i @ 50Hz
//    {26, dsVIDEO_PIXELRES_2880x576},  // 2880x576i @ 50Hz
//    {27, dsVIDEO_PIXELRES_2880x288},  // 2880x288p @ 50Hz
//    {28, dsVIDEO_PIXELRES_2880x288},  // 2880x288p @ 50Hz
//    {29, dsVIDEO_PIXELRES_1440x576},  // 1440x576p @ 50Hz
//    {30, dsVIDEO_PIXELRES_1440x576},  // 1440x576p @ 50Hz
//    {36, dsVIDEO_PIXELRES_2880x576},  // 2880x576p @ 50Hz
    {40, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 100Hz
    {41, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 100Hz
    {42, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 100Hz
    {43, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 100Hz
    {50, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 200Hz
    {51, dsVIDEO_PIXELRES_720x576},   // 720x576p @ 200Hz
    {52, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 200Hz
    {53, dsVIDEO_PIXELRES_720x576},   // 720x576i @ 200Hz

    // 720p resolutions
    {4, dsVIDEO_PIXELRES_1280x720},   // 1280x720p @ 59.94/60Hz
    {19, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 50Hz
    {39, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 100Hz
    {45, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 120Hz
    {58, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 24Hz
    {59, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 25Hz
    {60, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 30Hz
    {63, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 24Hz
    {64, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 25Hz
    {65, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 30Hz
    {66, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 50Hz
    {67, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 60Hz
    {68, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 100Hz
    {69, dsVIDEO_PIXELRES_1280x720},  // 1280x720p @ 120Hz

    // 1080i resolutions
    {5, dsVIDEO_PIXELRES_1920x1080},  // 1920x1080i @ 59.94/60Hz
    {20, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080i @ 50Hz
    {37, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080i @ 50Hz
    {38, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080i @ 100Hz
    {44, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080i @ 120Hz

    // 1080p resolutions
    {16, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 59.94/60Hz
    {31, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 50Hz
    {32, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 24Hz
    {33, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 25Hz
    {34, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 30Hz
    {44, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 120Hz
    {61, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 120Hz
    {62, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 100Hz
    {70, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 24Hz
    {71, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 25Hz
    {72, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 30Hz
    {73, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 50Hz
    {74, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 60Hz
    {75, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 100Hz
    {76, dsVIDEO_PIXELRES_1920x1080}, // 1920x1080p @ 120Hz

    // 2160p resolutions
    {77, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 24Hz
    {78, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 25Hz
    {79, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 30Hz
    {80, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 50Hz
    {81, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 60Hz
    {87, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 24Hz
    {88, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 25Hz
    {89, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 30Hz
    {90, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 50Hz
    {91, dsVIDEO_PIXELRES_3840x2160}, // 3840x2160p @ 60Hz

    // 4K resolutions
    {82, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 24Hz
    {83, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 25Hz
    {84, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 30Hz
    {85, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 50Hz
    {86, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 60Hz
    {92, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 24Hz
    {93, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 25Hz
    {94, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 30Hz
    {95, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 50Hz
    {96, dsVIDEO_PIXELRES_4096x2160}, // 4096x2160p @ 60Hz
};

#define VIC_MAP_TABLE_SIZE (sizeof(vicMapTable) / sizeof(VicMapEntry))

static uint16_t initialised = 0;
VCHI_INSTANCE_T vchi_instance;
VCHI_CONNECTION_T *vchi_connection;

int vchi_tv_init()
{
    hal_info("invoked.\n");
    int res = 0;
    if (!initialised)
    {
        vcos_init();
        res = vchi_initialise(&vchi_instance);
        if (res != 0)
        {
            hal_err("Failed to initialize VCHI (res=%d)\n", res);
            return res;
        }

        res = vchi_connect(NULL, 0, vchi_instance);
        if (res != 0)
        {
            hal_err("Failed to create VCHI connection (ret=%d)\n", res);
            return res;
        }

        // Initialize the tvservice
        vc_vchi_tv_init(vchi_instance, &vchi_connection, 1);
        // Initialize the gencmd
        vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1);
        initialised = 1;
    }
    return res;
}

int vchi_tv_uninit()
{
    hal_info("invoked.\n");
    int res = 0;
    if (initialised)
    {
        // Stop the tvservice
        vc_vchi_tv_stop();
        vc_gencmd_stop();
        // Disconnect the VCHI connection
        vchi_disconnect(vchi_instance);
        initialised = 0;
    }
    return res;
}

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
    if (!edidBytes || memcmp(edidBytes, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00", 8))
    {
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

bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize)
{
    char buffer[256] = {0};
    if (NULL == cmd || NULL == resp) {
        return false;
    }
    FILE *fp = popen(cmd, "r");
    if (NULL != fp) {
        size_t totalLen = 0;
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            printf("Read '%s'\n", buffer);
            size_t len = strlen(buffer);
            if (totalLen + len < respSize - 1) {
                strncpy(resp + totalLen, buffer, len);
                totalLen += len;
            } else {
                hal_warn("Response buffer is overflowing.\n");
                strncpy(resp + totalLen, buffer, respSize - totalLen - 1);
                totalLen = respSize - 1;
                break;
            }
        }
        resp[totalLen] = '\0';
        pclose(fp);
        return true;
    }
    return false;
}

const dsVideoResolution_t* getResolutionFromVic(int vic)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].vic == vic) {
            return &vicMapTable[i].resolution;
        }
    }
    return NULL; // VIC not found
}

const int* getVicFromResolution(dsVideoResolution_t resolution)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].resolution == resolution) {
            return &vicMapTable[i].vic;
        }
    }
    return NULL; // VIC not found
}
