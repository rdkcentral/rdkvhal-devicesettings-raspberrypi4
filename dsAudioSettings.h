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

#ifndef _DS_AUDIOOUTPUTPORTSETTINGS_H
#define _DS_AUDIOOUTPUTPORTSETTINGS_H

#include "dsUtl.h"
#include "dsTypes.h"

#ifndef DS_SETTINGS_FALLBACK_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define DS_SETTINGS_FALLBACK_UNUSED __attribute__((unused))
#else
#define DS_SETTINGS_FALLBACK_UNUSED
#endif
#endif

#ifdef DS_HAL_EXPORT_CONFIG_SYMBOLS
extern dsAudioTypeConfig_t  kAudioConfigs[];
extern dsAudioPortConfig_t  kAudioPorts[];
extern int                  kAudioConfigs_size;
extern int                  kAudioPorts_size;
#else /* !DS_HAL_EXPORT_CONFIG_SYMBOLS */
/*
 * Static fallback tables for devicesettings compile-time usage (dsUTL_DIM on kConfigs/kPorts).
 * Runtime path still uses dlsym symbols from HAL exported tables when available.
 */
static dsAudioEncoding_t kFallbackHDMIEncodings[] DS_SETTINGS_FALLBACK_UNUSED = { dsAUDIO_ENC_PCM, dsAUDIO_ENC_AC3 };
static dsAudioCompression_t kFallbackHDMICompressions[] DS_SETTINGS_FALLBACK_UNUSED = {
	dsAUDIO_CMP_NONE, dsAUDIO_CMP_LIGHT, dsAUDIO_CMP_MEDIUM, dsAUDIO_CMP_HEAVY,
};
static dsAudioStereoMode_t kFallbackHDMIStereoModes[] DS_SETTINGS_FALLBACK_UNUSED = {
	dsAUDIO_STEREO_STEREO, dsAUDIO_STEREO_SURROUND,
};

static dsAudioTypeConfig_t kConfigs[] DS_SETTINGS_FALLBACK_UNUSED = {
	{
		dsAUDIOPORT_TYPE_HDMI,
		"HDMI",
		dsUTL_DIM(kFallbackHDMICompressions),
		kFallbackHDMICompressions,
		dsUTL_DIM(kFallbackHDMIEncodings),
		kFallbackHDMIEncodings,
		dsUTL_DIM(kFallbackHDMIStereoModes),
		kFallbackHDMIStereoModes,
	},
};

static dsAudioPortConfig_t kPorts[] DS_SETTINGS_FALLBACK_UNUSED = {
	{
		{dsAUDIOPORT_TYPE_HDMI, 0},
		NULL,
	},
};
#endif /* !DS_HAL_EXPORT_CONFIG_SYMBOLS */

#endif /* _DS_AUDIOOUTPUTPORTSETTINGS_H */
