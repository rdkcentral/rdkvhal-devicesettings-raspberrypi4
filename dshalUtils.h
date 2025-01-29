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
*/

#ifndef __DSHALUTILS_H
#define __DSHALUTILS_H

#include "interface/vmcs_host/vc_tvservice.h"
#include "interface/vmcs_host/vc_vchi_gencmd.h"
#include "dsTypes.h"
#include "dsAVDTypes.h"

typedef struct {
    int vic;
    dsVideoResolution_t resolution;
} VicMapEntry;

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

int vchi_tv_init();
int vchi_tv_uninit();
int fill_edid_struct(unsigned char *edid, dsDisplayEDID_t *display, int size);
bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize);
const dsVideoResolution_t *getResolutionFromVic(int vic);
const int *getVicFromResolution(dsVideoResolution_t resolution);

#endif
