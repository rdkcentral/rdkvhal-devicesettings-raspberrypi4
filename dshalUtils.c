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

#include "dshalUtils.h"
#include "dshalLogger.h"
#include "dsVideoResolutionSettings.h"

const hdmiSupportedRes_t resolutionMap[] = {
    {"480p", 2},       // 720x480p @ 59.94/60Hz
    {"480p", 3},       // 720x480p @ 59.94/60Hz
    {"480i", 6},       // 720x480i @ 59.94/60Hz
    {"480i", 7},       // 720x480i @ 59.94/60Hz
    {"576p", 17},      // 720x576p @ 50Hz
    {"576p", 18},      // 720x576p @ 50Hz
    {"576i", 21},      // 720x576i @ 50Hz
    {"576i", 22},      // 720x576i @ 50Hz
    {"720p", 4},       // 1280x720p @ 59.94/60Hz
    {"720p50", 19},    // 1280x720p @ 50Hz
    {"1080i", 5},      // 1920x1080i @ 59.94/60Hz
	{"1080i60", 5},    // 1920x1080i @ 59.94/60Hz
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

const size_t  noOfItemsInResolutionMap = sizeof(resolutionMap) / sizeof(hdmiSupportedRes_t);
const size_t noOfItemsInkResolutions = sizeof(kResolutions) / sizeof(dsVideoPortResolution_t);

const WesterosReskResMap_t westerosReskResMap[] = {
    {"480p", "720x480px60"},
    {"480i", "720x480ix60"},
    {"576p", "720x576px50"},
    {"576i", "720x576ix50"},
    {"720p", "1280x720px60"},
    {"720p50", "1280x720px50"},
    {"1080p24", "1920x1080p24"},
    {"1080p25", "1920x1080p25"},
    {"1080p30", "1920x1080p30"},
    {"1080p50", "1920x1080p50"},
    {"1080p60", "1920x1080p60"},
    {"1080i", "1920x1080ix60"},
    {"1080i50", "1920x1080ix50"},
    {"2160p24", "3840x2160px24"},
    {"2160p25", "3840x2160px25"},
    {"2160p30", "3840x2160px30"},
    {"2160p50", "3840x2160px50"},
    {"2160p60", "3840x2160px60"}
};

dsVideoPortResolution_t *dsGetkResolutionByName(const char *name)
{
	for (size_t i = 0; i < noOfItemsInkResolutions; i++) {
		if (strcmp(kResolutions[i].name, name) == 0) {
			return &kResolutions[i];
		}
	}
	return NULL;
}

dsVideoPortResolution_t *dsGetkResolutionByPixelResolutionAndFrameRate(dsVideoResolution_t pixelResolution, dsVideoFrameRate_t frameRate, bool scanMode)
{
	for (size_t i = 0; i < noOfItemsInkResolutions; i++) {
		if ((kResolutions[i].pixelResolution == pixelResolution) && (kResolutions[i].frameRate == frameRate)
			&& (kResolutions[i].interlaced == scanMode)) {
			hal_dbg("Found matching resolution: %s\n", kResolutions[i].name);
			return &kResolutions[i];
		}
	}
	return NULL;
}

bool convertWesterosResolutionTokResolution(const char *westerosRes, dsVideoPortResolution_t *kResolution)
{
	dsVideoPortResolution_t *localkResolution = NULL;
	if (westerosRes == NULL || kResolution == NULL) {
		return false;
	}
	int Width = 0;
	int Height = 0;
	int FrameRate = 0;
	char ilaced = 0;
	if (sscanf(westerosRes, "%dx%d%cx%d", &Width, &Height, &ilaced, &FrameRate) == 4 ||
	    sscanf(westerosRes, "%dx%d%cx%d", &Width, &Height,  &ilaced, &FrameRate) == 4) {
		hal_dbg("Width: %d, Height: %d, FrameRate: %d, ilaced = %c\n", Width, Height, FrameRate, ilaced);
		localkResolution->pixelResolution = getdsVideoResolution(Width, Height);
		hal_dbg("PixelResolution: %d\n", localkResolution->pixelResolution);
		if (localkResolution->pixelResolution != dsVIDEO_PIXELRES_MAX) {
			localkResolution->frameRate = getdsVideoFrameRate(FrameRate);
			hal_dbg("FrameRate: %d\n", kResolution->frameRate);
			localkResolution = dsGetkResolutionByPixelResolutionAndFrameRate(localkResolution->pixelResolution, localkResolution->frameRate, (ilaced == 'p' ? false : true));
			if (localkResolution != NULL) {
				memcpy(kResolution, localkResolution, sizeof(dsVideoPortResolution_t));
				return true;
			}
		}
	}
	return false;
}

bool convertkResolutionToWesterosResolution(const dsVideoPortResolution_t *kResolution, char *westerosRes, size_t size)
{
	if (kResolution == NULL || westerosRes == NULL) {
		return false;
	}
	// get from westerosReskResMap
	for (size_t i = 0; i < sizeof(westerosReskResMap) / sizeof(WesterosReskResMap_t); i++) {
		if (strcmp(kResolution->name, westerosReskResMap[i].dsVideoPortResolutionName) == 0) {
			snprintf(westerosRes, size, "%s", westerosReskResMap[i].westerosResolution);
			return true;
		}
	}
	return false;
}

const VicMapEntry vicMapTable[] = {
    // 480i resolutions
    {6, dsTV_RESOLUTION_480i, "720x480ix60" },    // 720x480i @ 59.94/60Hz
    {7, dsTV_RESOLUTION_480i, "720x480ix60" },    // 720x480i @ 59.94/60Hz
    {48, dsTV_RESOLUTION_480i, "720x480ix120" },   // 720x480i @ 120Hz
    {49, dsTV_RESOLUTION_480i, "720x480ix120" },   // 720x480i @ 120Hz
    {56, dsTV_RESOLUTION_480i, "720x480ix240" },   // 720x480i @ 240Hz
    {57, dsTV_RESOLUTION_480i, "720x480ix240" },   // 720x480i @ 240Hz

    // 480p resolutions
    {2, dsTV_RESOLUTION_480p, "720x480px60" },    // 720x480p @ 59.94/60Hz
    {3, dsTV_RESOLUTION_480p, "720x480px60" },    // 720x480p @ 59.94/60Hz
    {46, dsTV_RESOLUTION_480p, "720x480px120" },   // 720x480p @ 120Hz
    {47, dsTV_RESOLUTION_480p, "720x480px120" },   // 720x480p @ 120Hz
    {54, dsTV_RESOLUTION_480p, "720x480px240" },   // 720x480p @ 240Hz
    {55, dsTV_RESOLUTION_480p, "720x480px240" },   // 720x480p @ 240Hz

    // 576i resolutions
    {21, dsTV_RESOLUTION_576i, "720x576ix50" },   // 720x576i @ 50Hz
    {22, dsTV_RESOLUTION_576i, "720x576ix50" },   // 720x576i @ 50Hz
    {42, dsTV_RESOLUTION_576i, "720x576ix100" },   // 720x576i @ 100Hz
    {43, dsTV_RESOLUTION_576i, "720x576ix100" },   // 720x576i @ 100Hz
    {52, dsTV_RESOLUTION_576i, "720x576ix200" },   // 720x576i @ 200Hz
    {53, dsTV_RESOLUTION_576i, "720x576ix200" },   // 720x576i @ 200Hz

    // 576p resolutions
    {17, dsTV_RESOLUTION_576p, "720x576px50" },   // 720x576p @ 50Hz
    {18, dsTV_RESOLUTION_576p, "720x576px50" },   // 720x576p @ 50Hz
    {40, dsTV_RESOLUTION_576p, "720x576px100" },   // 720x576p @ 100Hz
    {41, dsTV_RESOLUTION_576p, "720x576px100" },   // 720x576p @ 100Hz
    {50, dsTV_RESOLUTION_576p, "720x576px200" },   // 720x576p @ 200Hz
    {51, dsTV_RESOLUTION_576p,  "720x576px200" },   // 720x576p @ 200Hz

    // 720p resolutions
    {4, dsTV_RESOLUTION_720p, "1280x720px60" },    // 1280x720p @ 59.94/60Hz
    {19, dsTV_RESOLUTION_720p50, "1280x720px50" }, // 1280x720p @ 50Hz
    {39, dsTV_RESOLUTION_720p, "1280x720px100" },   // 1280x720p @ 100Hz
    {45, dsTV_RESOLUTION_720p, "1280x720px120" },   // 1280x720p @ 120Hz
    {58, dsTV_RESOLUTION_720p, "1280x720px24" },   // 1280x720p @ 24Hz
    {59, dsTV_RESOLUTION_720p, "1280x720px25" },   // 1280x720p @ 25Hz
    {60, dsTV_RESOLUTION_720p, "1280x720px30" },   // 1280x720p @ 30Hz
    {63, dsTV_RESOLUTION_720p, "1280x720px24" },   // 1280x720p @ 24Hz
    {64, dsTV_RESOLUTION_720p, "1280x720px25" },   // 1280x720p @ 25Hz
    {65, dsTV_RESOLUTION_720p, "1280x720px30" },   // 1280x720p @ 30Hz
    {66, dsTV_RESOLUTION_720p50, "1280x720px50" }, // 1280x720p @ 50Hz
    {67, dsTV_RESOLUTION_720p, "1280x720px60" },   // 1280x720p @ 60Hz
    {68, dsTV_RESOLUTION_720p, "1280x720px100" },   // 1280x720p @ 100Hz
    {69, dsTV_RESOLUTION_720p, "1280x720px120" },   // 1280x720p @ 120Hz

    // 1080i resolutions
    {5, dsTV_RESOLUTION_1080i, "1920x1080ix60" },   // 1920x1080i @ 59.94/60Hz
    {20, dsTV_RESOLUTION_1080i50, "1920x1080ix50" },// 1920x1080i @ 50Hz
    {37, dsTV_RESOLUTION_1080i50, "1920x1080ix50" },// 1920x1080i @ 50Hz
    {38, dsTV_RESOLUTION_1080i, "1920x1080ix100" },  // 1920x1080i @ 100Hz
    {44, dsTV_RESOLUTION_1080i, "1920x1080ix120" },  // 1920x1080i @ 120Hz

    // 1080p resolutions
    {16, dsTV_RESOLUTION_1080p, "1920x1080px60" },  // 1920x1080p @ 59.94/60Hz
    {31, dsTV_RESOLUTION_1080p50, "1920x1080px50" },// 1920x1080p @ 50Hz
    {32, dsTV_RESOLUTION_1080p24, "1920x1080px24" },// 1920x1080p @ 24Hz
    {33, dsTV_RESOLUTION_1080p25, "1920x1080px25" },// 1920x1080p @ 25Hz
    {34, dsTV_RESOLUTION_1080p30, "1920x1080px30" },// 1920x1080p @ 30Hz
    {44, dsTV_RESOLUTION_1080p, "1920x1080px120" },  // 1920x1080p @ 120Hz
    {61, dsTV_RESOLUTION_1080p, "1920x1080px120" },  // 1920x1080p @ 120Hz
    {62, dsTV_RESOLUTION_1080p, "1920x1080px100" },  // 1920x1080p @ 100Hz
    {70, dsTV_RESOLUTION_1080p24, "1920x1080px24" },// 1920x1080p @ 24Hz
    {71, dsTV_RESOLUTION_1080p25, "1920x1080px25" },// 1920x1080p @ 25Hz
    {72, dsTV_RESOLUTION_1080p30, "1920x1080px30" },// 1920x1080p @ 30Hz
    {73, dsTV_RESOLUTION_1080p50, "1920x1080px50" },// 1920x1080p @ 50Hz
    {74, dsTV_RESOLUTION_1080p60, "1920x1080px60" },// 1920x1080p @ 60Hz
    {75, dsTV_RESOLUTION_1080p, "1920x1080px100" },  // 1920x1080p @ 100Hz
    {76, dsTV_RESOLUTION_1080p, "1920x1080px120" },  // 1920x1080p @ 120Hz

    // 2160p resolutions
    {77, dsTV_RESOLUTION_2160p24, "3840x2160px24" },// 3840x2160p @ 24Hz
    {78, dsTV_RESOLUTION_2160p25, "3840x2160px25" },// 3840x2160p @ 25Hz
    {79, dsTV_RESOLUTION_2160p30, "3840x2160px30" },// 3840x2160p @ 30Hz
    {80, dsTV_RESOLUTION_2160p50, "3840x2160px50" },// 3840x2160p @ 50Hz
    {81, dsTV_RESOLUTION_2160p60, "3840x2160px60" },// 3840x2160p @ 60Hz
    {87, dsTV_RESOLUTION_2160p24, "3840x2160px24" },// 3840x2160p @ 24Hz
    {88, dsTV_RESOLUTION_2160p25, "3840x2160px25" },// 3840x2160p @ 25Hz
    {89, dsTV_RESOLUTION_2160p30, "3840x2160px30" },// 3840x2160p @ 30Hz
    {90, dsTV_RESOLUTION_2160p50, "3840x2160px50" },// 3840x2160p @ 50Hz
    {91, dsTV_RESOLUTION_2160p60, "3840x2160px60" },// 3840x2160p @ 60Hz

    // 4K resolutions
    {82, dsTV_RESOLUTION_2160p24, "4096x2160px24" },// 4096x2160p @ 24Hz
    {83, dsTV_RESOLUTION_2160p25, "4096x2160px25" },// 4096x2160p @ 25Hz
    {84, dsTV_RESOLUTION_2160p30, "4096x2160px30" },// 4096x2160p @ 30Hz
    {85, dsTV_RESOLUTION_2160p50, "4096x2160px50" },// 4096x2160p @ 50Hz
    {86, dsTV_RESOLUTION_2160p60, "4096x2160px60" },// 4096x2160p @ 60Hz
    {93, dsTV_RESOLUTION_2160p24, "4096x2160px24" },// 4096x2160p @ 24Hz
    {94, dsTV_RESOLUTION_2160p25, "4096x2160px25" },// 4096x2160p @ 25Hz
    {95, dsTV_RESOLUTION_2160p30, "4096x2160px30" },// 4096x2160p @ 30Hz
    {98, dsTV_RESOLUTION_2160p24, "4096x2160px24" },// 4096x2160p @ 24Hz
    {99, dsTV_RESOLUTION_2160p25, "4096x2160px25" },// 4096x2160p @ 25Hz
    {100, dsTV_RESOLUTION_2160p30, "4096x2160px30" },// 4096x2160p @ 30Hz
    {101, dsTV_RESOLUTION_2160p50, "4096x2160px50" },// 4096x2160p @ 50Hz
    {102, dsTV_RESOLUTION_2160p60, "4096x2160px60" },// 4096x2160p @ 60Hz
};

#define VIC_MAP_TABLE_SIZE (sizeof(vicMapTable) / sizeof(VicMapEntry))

char *getdsVideoFrameRateString(dsVideoFrameRate_t framerate)
{
	switch (framerate) {
		case dsVIDEO_FRAMERATE_24: return "24";
		case dsVIDEO_FRAMERATE_25: return "25";
		case dsVIDEO_FRAMERATE_30: return "30";
		case dsVIDEO_FRAMERATE_60: return "60";
		case dsVIDEO_FRAMERATE_23dot98: return "23.98";
		case dsVIDEO_FRAMERATE_29dot97: return "29.97";
		case dsVIDEO_FRAMERATE_50: return "50";
		case dsVIDEO_FRAMERATE_59dot94: return "59.94";
		default: return NULL;
	}
}

dsVideoFrameRate_t getdsVideoFrameRate(uint16_t frameRate)
{
	switch (frameRate) {
		case 24: return dsVIDEO_FRAMERATE_24;
		case 25: return dsVIDEO_FRAMERATE_25;
		case 30: return dsVIDEO_FRAMERATE_30;
		case 60: return dsVIDEO_FRAMERATE_60;
		case 23: return dsVIDEO_FRAMERATE_23dot98;
		case 29: return dsVIDEO_FRAMERATE_29dot97;
		case 50: return dsVIDEO_FRAMERATE_50;
		case 59: return dsVIDEO_FRAMERATE_59dot94;
		default: return dsVIDEO_FRAMERATE_MAX;
	}
}

int gcd(int a, int b) {
    while (b != 0) {
        int temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

dsVideoAspectRatio_t getAspectRatioFromWidthHeight(int width, int height)
{
    if (width == 0 || height == 0) {
        return dsVIDEO_ASPECT_RATIO_MAX;
    }
    int divisor = gcd(width, height);
    int aspectWidth = width / divisor;
    int aspectHeight = height / divisor;

    if (aspectWidth == 4 && aspectHeight == 3) {
        return dsVIDEO_ASPECT_RATIO_4x3;
    } else if (aspectWidth == 16 && aspectHeight == 9) {
        return dsVIDEO_ASPECT_RATIO_16x9;
    } else {
        return dsVIDEO_ASPECT_RATIO_MAX;
    }
}

dsVideoAspectRatio_t getdsVideoAspectRatio(uint16_t aspectRatio)
{
	//Ref: https://github.com/raspberrypi/userland/blob/master/interface/vmcs_host/vc_hdmi.h#L73
	// rdk only supports 4:3 and 16:9 aspect ratios
	switch (aspectRatio) {
		case 1:	return dsVIDEO_ASPECT_RATIO_4x3;
		case 3: return dsVIDEO_ASPECT_RATIO_16x9;
		default:
			return dsVIDEO_ASPECT_RATIO_MAX;
	}
}

dsVideoResolution_t getdsVideoResolution(uint32_t width, uint32_t height)
{
	if (width == 720 && height == 480)
		return dsVIDEO_PIXELRES_720x480;
	else if (width == 720 && height == 576)
		return dsVIDEO_PIXELRES_720x576;
	else if (width == 1280 && height == 720)
		return dsVIDEO_PIXELRES_1280x720;
	else if (width == 1366 && height == 768)
		return dsVIDEO_PIXELRES_1366x768;
	else if (width == 1920 && height == 1080)
		return dsVIDEO_PIXELRES_1920x1080;
	else if (width == 3840 && height == 2160)
		return dsVIDEO_PIXELRES_3840x2160;
	else if (width == 4096 && height == 2160)
		return dsVIDEO_PIXELRES_4096x2160;
	else
		return dsVIDEO_PIXELRES_MAX;
}

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
        if (vc_vchi_tv_init(vchi_instance, &vchi_connection, 1) < 0) {
			hal_err("Failed to initialize vc_vchi_tv_init()\n");
			return -1;
		}
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

// const char *getWesterosResolutionFromVic(int vic)
// {
//     for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
//         if (vicMapTable[i].vic == vic) {
//             return vicMapTable[i].westerosResolution;
//         }
//     }
//     return NULL; // VIC not found
// }

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
