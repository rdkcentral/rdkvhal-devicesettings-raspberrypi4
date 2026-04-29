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

#ifndef _DS_VIDEORESOLUTIONSETTINGS_H_
#define _DS_VIDEORESOLUTIONSETTINGS_H_

#include "dsTypes.h"

#define  _INTERLACED true
#define _PROGRESSIVE false

#ifndef DS_SETTINGS_FALLBACK_UNUSED
#if defined(__GNUC__) || defined(__clang__)
#define DS_SETTINGS_FALLBACK_UNUSED __attribute__((unused))
#else
#define DS_SETTINGS_FALLBACK_UNUSED
#endif
#endif

#ifdef DS_HAL_EXPORT_CONFIG_SYMBOLS
extern dsVideoPortResolution_t kResolutionsSettings[];
extern int                     kResolutionsSettings_size;
extern size_t                  kNumResolutionsSettings;
extern int                     kDefaultResIndex;
#else /* !DS_HAL_EXPORT_CONFIG_SYMBOLS */
/* Static fallback table for middleware compile-time dsUTL_DIM(kResolutions). */
static dsVideoPortResolution_t kResolutions[] DS_SETTINGS_FALLBACK_UNUSED = {
	{
		"720p",
		dsVIDEO_PIXELRES_1280x720,
		dsVIDEO_ASPECT_RATIO_16x9,
		dsVIDEO_SSMODE_2D,
		dsVIDEO_FRAMERATE_60,
		_PROGRESSIVE,
	},
};

static int kDefaultResIndex DS_SETTINGS_FALLBACK_UNUSED = 0;

/* Compatibility aliases expected by some HAL C files */
#define kResolutionsSettings kResolutions
static int kResolutionsSettings_size DS_SETTINGS_FALLBACK_UNUSED = (int)(sizeof(kResolutions)/sizeof(kResolutions[0]));
static size_t kNumResolutionsSettings DS_SETTINGS_FALLBACK_UNUSED = sizeof(kResolutions)/sizeof(kResolutions[0]);
#endif /* !DS_HAL_EXPORT_CONFIG_SYMBOLS */

#endif /* _DS_VIDEORESOLUTIONSETTINGS_H_ */
