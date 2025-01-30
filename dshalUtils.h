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
    dsTVResolution_t tvresolution;
} VicMapEntry;

typedef struct {
    uint8_t header[8];
    uint16_t manufacturer_id;
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t week_of_manufacture;
    uint16_t year_of_manufacture;
    uint8_t edid_version;
    uint8_t edid_revision;
    uint8_t basic_display_params[5];
    uint8_t chromaticity_coords[10];
    uint8_t established_timings[3];
    uint8_t standard_timings[16];
    uint8_t detailed_timing_descriptors[72];
    uint8_t extension_flag;
    uint8_t checksum;
} EDID_t;

/* List all supported resolutions here */
typedef struct __hdmiSupportedRes_t {
    const char *rdkRes;
    int mode;
} hdmiSupportedRes_t;

static hdmiSupportedRes_t resolutionMap[] = {
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
    {"1080i50", 20},   // 1920x1080i @ 50Hz
    {"1080p24", 32},   // 1920x1080p @ 24Hz
    {"1080p25", 33},   // 1920x1080p @ 25Hz
    {"1080p30", 34},   // 1920x1080p @ 30Hz
    {"1080p50", 31},   // 1920x1080p @ 50Hz
    {"1080p60", 16},   // 1920x1080p @ 59.94/60Hz
    {"2160p24", 93},   // 3840x2160p @ 24Hz
    {"2160p25", 94},   // 3840x2160p @ 25Hz
    {"2160p30", 95},   // 3840x2160p @ 30Hz
    {"2160p50", 99},   // 4096x2160p @ 50Hz
    {"2160p60", 100},  // 4096x2160p @ 60Hz
    {"2160p24", 98},   // 4096x2160p @ 24Hz
    {"2160p25", 99},   // 4096x2160p @ 25Hz
    {"2160p30", 100},  // 4096x2160p @ 30Hz
    {"2160p50", 101},  // 4096x2160p @ 50Hz
    {"2160p60", 102}   // 4096x2160p @ 60Hz
};

int vchi_tv_init();
int vchi_tv_uninit();
int fill_edid_struct(unsigned char *edid, dsDisplayEDID_t *display, int size);
void parse_edid(const uint8_t *edid, EDID_t *parsed_edid);
void print_edid(const EDID_t *parsed_edid);
bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize);
const dsTVResolution_t *getResolutionFromVic(int vic);
const int *getVicFromResolution(dsTVResolution_t resolution);

#endif
