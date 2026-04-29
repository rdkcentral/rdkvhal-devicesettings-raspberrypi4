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
 * @file dsVideoPortSettingsData.c
 * @brief Definitions of HAL-exported video port configuration tables.
 *
 * kVideoPortConfigs and kVideoPortPorts are resolved at runtime by the middleware
 * via dlsym() / LoadDLSymbols().  kResolutionsSettings is referenced here as an
 * extern symbol exported by dsVideoResolutionSettingsData.c; the linker resolves
 * the relocation when building the shared library.
 */

#include "dsTypes.h"
#include "dsUtl.h"
#define DS_HAL_EXPORT_CONFIG_SYMBOLS
#include "dsVideoPortSettings.h"  /* pulls in dsVideoResolutionSettings.h for kResolutionsSettings extern */

/* Exported configuration tables - looked up via dlsym() by the middleware */
dsVideoPortTypeConfig_t kVideoPortConfigs[] = {
    {
        /*.typeId = */                  dsVIDEOPORT_TYPE_HDMI,
        /*.name = */                    "HDMI",
        /*.dtcpSupported = */           false,
        /*.hdcpSupported = */           false,
        /*.restrictedResolution = */    -1,
        /*.numSupportedResolutions = */ 0, /* 0 means "info available at runtime" */
        /*.supportedResolutons = */     kResolutionsSettings,
    },
};

dsVideoPortPortConfig_t kVideoPortPorts[] = {
    {
        /*.typeId = */              {dsVIDEOPORT_TYPE_HDMI, 0},
        /*.connectedAOP = */        {dsAUDIOPORT_TYPE_HDMI, 0},
        /*.defaultResolution = */   "720p"
    },
};

int kVideoPortConfigs_size = sizeof(kVideoPortConfigs) / sizeof(kVideoPortConfigs[0]);
int kVideoPortPorts_size   = sizeof(kVideoPortPorts)   / sizeof(kVideoPortPorts[0]);
