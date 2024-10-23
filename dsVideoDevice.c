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

#include <stdlib.h>
#include <stdio.h>
#include "dsTypes.h"
#include "dsVideoDevice.h"

dsError_t  dsVideoDeviceInit()
{
	dsError_t ret = dsERR_NONE;
	return ret;
}

dsError_t  dsGetVideoDevice(int index, intptr_t *handle)
{
	dsError_t ret = dsERR_NONE;

	*handle = 0;
	return ret;
}

dsError_t  dsSetDFC(intptr_t handle, dsVideoZoom_t dfc)
{
	dsError_t ret = dsERR_NONE;

	return ret;
}

dsError_t  dsGetDFC(intptr_t handle, dsVideoZoom_t *dfc)
{
	dsError_t ret = dsERR_NONE;

	return ret;
}

dsError_t  dsVideoDeviceTerm()
{
	dsError_t ret = dsERR_NONE;
	return ret;
}
dsError_t dsGetHDRCapabilities(intptr_t handle, int *capabilities)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetSupportedVideoCodingFormats(intptr_t handle, unsigned int * supported_formats)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetVideoCodecInfo(intptr_t handle, dsVideoCodingFormat_t codec, dsVideoCodecInfo_t * info)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsForceDisableHDRSupport(intptr_t handle, bool disable)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetFRFMode(intptr_t handle, int frfmode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetFRFMode(intptr_t handle, int *frfmode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetCurrentDisplayframerate(intptr_t handle, char *framerate)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetDisplayframerate(intptr_t handle, char *framerate)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsRegisterFrameratePreChangeCB(dsRegisterFrameratePreChangeCB_t CBFunc)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsRegisterFrameratePostChangeCB(dsRegisterFrameratePostChangeCB_t CBFunc)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
