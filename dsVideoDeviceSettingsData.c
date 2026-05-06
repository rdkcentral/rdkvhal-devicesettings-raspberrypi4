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

/**
 * @file dsVideoDeviceSettingsData.c
 * @brief Definitions of HAL-exported video device configuration tables.
 *
 * Resolved at runtime by the middleware via dlsym() / LoadDLSymbols().
 */

#include "dsUtl.h"
#include "dsTypes.h"
#include "dsVideoDeviceSettings.h"

/* Supporting arrays - file-local; not exported and not looked up by dlsym */
static dsVideoZoom_t kSupportedDFCs[] = {
    dsVIDEO_ZOOM_NONE, dsVIDEO_ZOOM_FULL, dsVIDEO_ZOOM_PLATFORM
};

/* Exported configuration tables - looked up via dlsym() by the middleware */
int kNumVideoDevices = 1;

dsVideoConfig_t kVideoDeviceConfigs[] = {
    {
        /*.numSupportedDFCs = */ dsUTL_DIM(kSupportedDFCs),
        /*.supportedDFCs = */    kSupportedDFCs,
        /*.defaultDFC = */       dsVIDEO_ZOOM_NONE,
    },
};

int kVideoDeviceConfigs_size = sizeof(kVideoDeviceConfigs) / sizeof(kVideoDeviceConfigs[0]);
