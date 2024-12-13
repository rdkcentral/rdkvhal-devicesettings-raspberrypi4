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
#include "dshalUtils.h"

dsError_t  dsVideoDeviceInit()
{
	dsError_t ret = dsERR_NONE;
	if(true == _bVideoDeviceInited)
        {
                return dsERR_ALREADY_INITIALIZED;
	}
	_bVideoDeviceInited = true;
	return ret;
}

dsError_t  dsGetVideoDevice(int index, intptr_t *handle)
{
	if(false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if (index != 0 || NULL == handle)
	{
		return = dsERR_INVALID_PARAM;
	}
	return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t  dsSetDFC(intptr_t handle, dsVideoZoom_t dfc)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if(!dsIsValidHandle(handle))
	{
		return dsERR_INVALID_PARAM;
	}
	return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t  dsGetDFC(intptr_t handle, dsVideoZoom_t *dfc)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if (NULL == dfc || !dsIsValidHandle(handle))
	{
		 return dsERR_INVALID_PARAM;
	}
	 return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t  dsVideoDeviceTerm()
{
	dsError_t ret = dsERR_NONE;
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	_bVideoDeviceInited = false;
	return ret;
}
dsError_t dsGetHDRCapabilities(intptr_t handle, int *capabilities)
{
	if (false == _bVideoDeviceInited)
	 {
		  return dsERR_NOT_INITIALIZED;
	 }
	if(capabilities == NULL || !dsIsValidHandle(handle))
	{
	   return dsERR_INVALID_PARAM;
	}
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetSupportedVideoCodingFormats(intptr_t handle, unsigned int * supported_formats)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if(supported_formats == NULL || !dsIsValidHandle(handle))
	{
		return dsERR_INVALID_PARAM;
	}
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetVideoCodecInfo(intptr_t handle, dsVideoCodingFormat_t codec, dsVideoCodecInfo_t * info)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if(info == NULL || !dsIsValidHandle(handle))
        {
           return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsForceDisableHDRSupport(intptr_t handle, bool disable)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
	if(!dsIsValidHandle(handle))
	{
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetFRFMode(intptr_t handle, int frfmode)
{
	if (false == _bVideoDeviceInited)
	 {
		 return dsERR_NOT_INITIALIZED;
	 }
	if(!dsIsValidHandle(handle) || frfmode < 0)
	{
		return dsERR_INVALID_PARAM;
	}
	return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetFRFMode(intptr_t handle, int *frfmode)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
        if(!dsIsValidHandle(handle) || frfmode == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetCurrentDisplayframerate(intptr_t handle, char *framerate)
{
	if (false == _bVideoDeviceInited)
	{
		return dsERR_NOT_INITIALIZED;
	}
        if(!dsIsValidHandle(handle) ||framerate == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetDisplayframerate(intptr_t handle, char *framerate)
{
	if (false == _bVideoDeviceInited)
	{
		 return dsERR_NOT_INITIALIZED;
	}
     	if(!dsIsValidHandle(handle) ||framerate == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsRegisterFrameratePreChangeCB(dsRegisterFrameratePreChangeCB_t CBFunc)
{
	if (false == _bVideoDeviceInited)
	{
		 return dsERR_NOT_INITIALIZED;
	}
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsRegisterFrameratePostChangeCB(dsRegisterFrameratePostChangeCB_t CBFunc)
{
	if (false == _bVideoDeviceInited)
	{
		 return dsERR_NOT_INITIALIZED;
	}
        return dsERR_OPERATION_NOT_SUPPORTED;
}
