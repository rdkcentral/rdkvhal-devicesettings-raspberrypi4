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

#include <stdio.h>
#include <string.h>
#include "dsHdmiIn.h"

dsError_t dsHdmiInInit (void)
{
    // no hdmi in, do nothing
    return dsERR_NONE;
}

dsError_t dsHdmiInTerm (void)
{
    // no hdmi in, do nothing
    return dsERR_NONE;
}

dsError_t dsHdmiInGetNumberOfInputs (uint8_t *pNumHdmiInputs)
{
    if (pNumHdmiInputs == NULL)
        return dsERR_INVALID_PARAM;
    // no hdmi in
    *pNumHdmiInputs = 0;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInGetStatus (dsHdmiInStatus_t *pStatus)
{
    if (pStatus == NULL)
        return dsERR_INVALID_PARAM;
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInSelectPort (dsHdmiInPort_t ePort , bool audioMix, dsVideoPlaneType_t evideoPlaneType,bool topMost)
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInScaleVideo (int32_t x, int32_t y, int32_t width, int32_t height)
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInSelectZoomMode (dsVideoZoom_t eZoomMode)
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInPauseAudio ()
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInResumeAudio ()
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterConnectCB (dsHdmiInConnectCB_t CBFunc)
{
    // no hdmi in, return not supported
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInGetCurrentVideoMode (dsVideoPortResolution_t *resolution)
{
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterSignalChangeCB (dsHdmiInSignalChangeCB_t CBFunc)
{
   return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterStatusChangeCB (dsHdmiInStatusChangeCB_t CBFunc)
{
   return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterVideoModeUpdateCB(dsHdmiInVideoModeUpdateCB_t CBFunc)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterAllmChangeCB (dsHdmiInAllmChangeCB_t CBFunc)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterAVLatencyChangeCB (dsAVLatencyChangeCB_t CBFunc)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsHdmiInRegisterAviContentTypeChangeCB (dsHdmiInAviContentTypeChangeCB_t CBFunc)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsIsHdmiARCPort (dsHdmiInPort_t iPort, bool *isArcPort)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetEDIDBytesInfo (dsHdmiInPort_t iHdmiPort, unsigned char *edid, int *length)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDMISPDInfo (dsHdmiInPort_t iHdmiPort, unsigned char *data)
{
  return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetEdidVersion (dsHdmiInPort_t iHdmiPort, tv_hdmi_edid_version_t iEdidVersion)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetEdidVersion (dsHdmiInPort_t iHdmiPort, tv_hdmi_edid_version_t *iEdidVersion)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetAllmStatus (dsHdmiInPort_t iHdmiPort, bool *allmStatus)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetSupportedGameFeaturesList (dsSupportedGameFeatureList_t* features)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetAVLatency (int *audio_latency, int *video_latency)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetEdid2AllmSupport (dsHdmiInPort_t iHdmiPort, bool allmSupport)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetEdid2AllmSupport (dsHdmiInPort_t iHdmiPort, bool *allmSupport)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHdmiVersion(dsHdmiInPort_t iHdmiPort, dsHdmiMaxCapabilityVersion_t *maxCompatibilityVersion)
{
 return dsERR_OPERATION_NOT_SUPPORTED;
}

