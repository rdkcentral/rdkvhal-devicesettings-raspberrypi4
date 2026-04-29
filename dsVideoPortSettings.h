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

#ifndef _DS_VIDEOOUTPUTPORTSETTINGS_H_
#define _DS_VIDEOOUTPUTPORTSETTINGS_H_

#include "dsTypes.h"
#include "dsUtl.h"
#include "dsVideoResolutionSettings.h"

#ifndef DS_SETTINGS_FALLBACK_UNUSED
#define DS_SETTINGS_FALLBACK_UNUSED __attribute__((unused))
#endif

#ifdef DS_HAL_EXPORT_CONFIG_SYMBOLS
extern dsVideoPortTypeConfig_t  kVideoPortConfigs[];
extern dsVideoPortPortConfig_t  kVideoPortPorts[];
extern int                      kVideoPortConfigs_size;
extern int                      kVideoPortPorts_size;
#else
/* Static fallback tables for middleware compile-time dsUTL_DIM(kConfigs/kPorts). */
static dsVideoPortTypeConfig_t kConfigs[] DS_SETTINGS_FALLBACK_UNUSED = {
	{
		dsVIDEOPORT_TYPE_HDMI,
		"HDMI",
		false,
		false,
		-1,
		dsUTL_DIM(kResolutions),
		kResolutions,
	},
};

static dsVideoPortPortConfig_t kPorts[] DS_SETTINGS_FALLBACK_UNUSED = {
	{
		{dsVIDEOPORT_TYPE_HDMI, 0},
		{dsAUDIOPORT_TYPE_HDMI, 0},
		"720p"
	},
};
#endif

#endif /* _DS_VIDEOOUTPUTPORTSETTINGS_H_ */
