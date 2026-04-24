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

/*
 * Setup the supported configurations here.
 */
dsAudioPortType_t 		kAudioSupportedPortTypes[] 			= { dsAUDIOPORT_TYPE_HDMI };
dsAudioEncoding_t 		kSupportedHDMIEncodings[]			= { dsAUDIO_ENC_PCM, dsAUDIO_ENC_AC3};
dsAudioCompression_t 	kSupportedHDMICompressions[] 		= { dsAUDIO_CMP_NONE, dsAUDIO_CMP_LIGHT, dsAUDIO_CMP_MEDIUM, dsAUDIO_CMP_HEAVY, };
dsAudioStereoMode_t 	kSupportedHDMIStereoModes[] 		= { dsAUDIO_STEREO_STEREO, dsAUDIO_STEREO_SURROUND, };

dsAudioTypeConfig_t 	kAudioConfigs[]= {
	{
		/*.typeId = */					dsAUDIOPORT_TYPE_HDMI,
		/*.name = */					"HDMI", //HDMI
		/*.numSupportedCompressions = */dsUTL_DIM(kSupportedHDMICompressions),
		/*.compressions = */			kSupportedHDMICompressions,
		/*.numSupportedEncodings = */	dsUTL_DIM(kSupportedHDMIEncodings),
		/*.encodings = */				kSupportedHDMIEncodings,
		/*.numSupportedStereoModes = */	dsUTL_DIM(kSupportedHDMIStereoModes),
		/*.stereoModes = */				kSupportedHDMIStereoModes,
	}
};

dsVideoPortPortId_t connectedVOPs[dsAUDIOPORT_TYPE_MAX][dsVIDEOPORT_TYPE_MAX] = {
	{/*VOPs connected to LR Audio */

	},
	{/*VOPs connected to HDMI Audio */
		{dsVIDEOPORT_TYPE_HDMI, 0},
	}
};

dsAudioPortConfig_t kAudioPorts[] = {
	{
		/*.typeId = */ 					{dsAUDIOPORT_TYPE_HDMI, 0},
		/*.connectedVOPs = */			connectedVOPs[dsAUDIOPORT_TYPE_HDMI],
	}
};

#endif
