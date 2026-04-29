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

#ifndef _DS_VIDEODEVICESETTINGS_H_
#define _DS_VIDEODEVICESETTINGS_H_

#include "dsUtl.h"
#include "dsTypes.h"

#ifdef DS_HAL_EXPORT_CONFIG_SYMBOLS
extern dsVideoConfig_t  kVideoDeviceConfigs[];
extern int              kVideoDeviceConfigs_size;
extern int              kNumVideoDevices;
#else
/* Static fallback tables for middleware compile-time dsUTL_DIM(kConfigs). */
static dsVideoZoom_t kFallbackSupportedDFCs[] = {
	dsVIDEO_ZOOM_NONE, dsVIDEO_ZOOM_FULL, dsVIDEO_ZOOM_PLATFORM
};

static const int kNumVideoDevices = 1;

static dsVideoConfig_t kConfigs[] = {
	{
		dsUTL_DIM(kFallbackSupportedDFCs),
		kFallbackSupportedDFCs,
		dsVIDEO_ZOOM_NONE,
	},
};

typedef int _SafetyCheck[(dsUTL_DIM(kConfigs) == kNumVideoDevices) ? 1 : -1];
#endif

#endif /* _DS_VIDEODEVICESETTINGS_H_ */
