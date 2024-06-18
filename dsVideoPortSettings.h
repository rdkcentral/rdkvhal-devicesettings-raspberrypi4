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


#ifdef __cplusplus
extern "C" {
#endif

namespace {
/*
 * Setup the supported configurations here.
 */
static const dsVideoPortType_t kSupportedPortTypes[] = { dsVIDEOPORT_TYPE_HDMI };

static const dsVideoPortTypeConfig_t kConfigs[]= {
		{
		/*.typeId = */					dsVIDEOPORT_TYPE_HDMI,
		/*.name = */ 					"HDMI",
		/*.dtcpSupported = */			false,
		/*.hdcpSupported = */			true,
		/*.restrictedResollution = */	-1,
		/*.numSupportedResolutions = */ dsUTL_DIM(kResolutions), // 0 means "Info available at runtime"
		/*.supportedResolutons = */     kResolutions,
		},
};

static const dsVideoPortPortConfig_t kPorts[] = {
		{
		/*.typeId = */ 					{dsVIDEOPORT_TYPE_HDMI, 0},
		/*.connectedAOP */              {dsAUDIOPORT_TYPE_HDMI, 0},
		/*.defaultResolution = */		"720p"
		},
};

}

#ifdef __cplusplus
}
#endif


#endif /* VIDEOOUTPUTPORTSETTINGS_H_ */
