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

extern "C" {
#include "interface/vmcs_host/vc_tvservice.h"
#include "interface/vmcs_host/vc_vchi_gencmd.h"
}
#include "dsTypes.h"

static hdmiSupportedRes_t resolutionMap[] = {
                {"480p", 3},
                {"576p50", 18},
                {"720p", 4},
                {"720p50", 19},
                {"1080i", 5},
                {"1080p", 33},
                {"1080i50", 20},
                {"1080p50", 31},
                {"1080p24", 32},
                {"1080p30", 34},
                {"1080p60", 16}
};

int vchi_tv_init();

int vchi_tv_uninit();

void fill_edid_struct(unsigned char *edid, dsDisplayEDID_t *display, int size);

bool dsIsValidHandle(intptr_t uHandle);

#endif
