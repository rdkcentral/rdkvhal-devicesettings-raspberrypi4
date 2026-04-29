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
 * @file dsAudioSettingsData.c
 * @brief Definitions of HAL audio configuration tables exported from the shared library.
 *
 * These symbols are resolved at runtime by the middleware via dlsym() / LoadDLSymbols().
 * Keeping definitions here (rather than in the header) ensures:
 *   - Each symbol has exactly one definition with external linkage in the HAL .so.
 *   - Middleware source files that include dsAudioSettings.h receive only extern
 *     declarations and never define the symbols themselves, eliminating any risk of
 *     multiple-definition errors regardless of whether those files are compiled as C or C++.
 */

#include "dsUtl.h"
#include "dsTypes.h"
#include "dsAudioSettings.h"

/* Supporting arrays - file-local; not exported and not looked up by dlsym */
static dsAudioEncoding_t    kSupportedHDMIEncodings[]    = { dsAUDIO_ENC_PCM, dsAUDIO_ENC_AC3 };
static dsAudioCompression_t kSupportedHDMICompressions[] = {
    dsAUDIO_CMP_NONE, dsAUDIO_CMP_LIGHT, dsAUDIO_CMP_MEDIUM, dsAUDIO_CMP_HEAVY,
};
static dsAudioStereoMode_t  kSupportedHDMIStereoModes[]  = {
    dsAUDIO_STEREO_STEREO, dsAUDIO_STEREO_SURROUND,
};

static dsVideoPortPortId_t connectedVOPs[dsAUDIOPORT_TYPE_MAX][dsVIDEOPORT_TYPE_MAX] = {
    { /* VOPs connected to LR Audio (none) */
    },
    { /* VOPs connected to HDMI Audio */
        {dsVIDEOPORT_TYPE_HDMI, 0},
    }
};

/* Exported configuration tables - resolved at runtime by LoadDLSymbols() */
dsAudioTypeConfig_t kAudioConfigs[] = {
    {
        /*.typeId = */                  dsAUDIOPORT_TYPE_HDMI,
        /*.name = */                    "HDMI",
        /*.numSupportedCompressions = */ dsUTL_DIM(kSupportedHDMICompressions),
        /*.compressions = */            kSupportedHDMICompressions,
        /*.numSupportedEncodings = */   dsUTL_DIM(kSupportedHDMIEncodings),
        /*.encodings = */               kSupportedHDMIEncodings,
        /*.numSupportedStereoModes = */ dsUTL_DIM(kSupportedHDMIStereoModes),
        /*.stereoModes = */             kSupportedHDMIStereoModes,
    }
};

dsAudioPortConfig_t kAudioPorts[] = {
    {
        /*.typeId = */        {dsAUDIOPORT_TYPE_HDMI, 0},
        /*.connectedVOPs = */ connectedVOPs[dsAUDIOPORT_TYPE_HDMI],
    }
};

int kAudioConfigs_size = sizeof(kAudioConfigs) / sizeof(kAudioConfigs[0]);
int kAudioPorts_size   = sizeof(kAudioPorts)   / sizeof(kAudioPorts[0]);
