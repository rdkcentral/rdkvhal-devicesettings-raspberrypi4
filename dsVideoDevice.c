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
#include "dshalLogger.h"

static bool _bVideoDeviceInited = false;

/**
 * @brief Initializes all the video devices in the system
 *
 * @return dsError_t                   - Status
 * @retval dsERR_NONE                   - Success
 * @retval dsERR_ALREADY_INITIALIZED    - Function is already initialized.
 * @retval dsERR_GENERAL                - Underlying undefined platform error
 *
 * @warning  This function is Not thread safe.
 * @see dsVideoDeviceTerm()
 */
dsError_t dsVideoDeviceInit()
{
    hal_info("invoked.\n");
    if (true == _bVideoDeviceInited)
    {
        return dsERR_ALREADY_INITIALIZED;
    }
    _bVideoDeviceInited = true;
    return dsERR_NONE;
}

/**
 * @brief Gets the handle for the video device requested
 *
 * @note Index is always 0, due to devices only having a single video device
 *
 * @param[in]  index    - Index of video device. Max number is device specific. Min of 0
 * @param[out] handle   - The handle used by the Caller to uniquely identify the HAL instance
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() must be called before calling this function
 *
 * @warning  This function is Not thread safe.
 */
dsError_t dsGetVideoDevice(int index, intptr_t *handle)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (index != 0 || NULL == handle)
    {
        return dsERR_INVALID_PARAM;
    }
    *handle = 0;
    return dsERR_NONE;
}

/**
 * @brief Sets the screen zoom mode (decoder format conversion)
 *
 * @param[in] handle    - The handle returned from the dsGetVideoDevice() function
 * @param[in] dfc       - Type of zoom mode to be used.  Please refer ::dsVideoZoom_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsGetDFC()
 */
dsError_t dsSetDFC(intptr_t handle, dsVideoZoom_t dfc)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the screen zoom mode (decoder format conversion)
 *
 * @param[in] handle    - The handle returned from the dsGetVideoDevice() function
 * @param[out] dfc      - Type of zoom mode being used.  Please refer ::dsVideoZoom_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsSetDFC()
 */
dsError_t dsGetDFC(intptr_t handle, dsVideoZoom_t *dfc)
{
    hal_info("invoked.\n");
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

/**
 * @brief De-initializes all the video devices in the system
 *
 * This function reset any data structures used within this module and
 * release any handles specific to the video devices.
 *
 * @return dsError_t             - Status
 * @retval dsERR_NONE            - Success
 * @retval dsERR_NOT_INITIALIZED - Module is not initialized
 * @retval dsERR_GENERAL         - General failure
 *
 * @pre dsVideoDeviceInit() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsVideoDeviceInit()
 *
 */
dsError_t dsVideoDeviceTerm()
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    _bVideoDeviceInited = false;
    return dsERR_NONE;
}

/**
 * @brief Gets the HDR capabilities
 *
 * @param[in]  handle       - The handle returned from the dsGetVideoDevice() function
 * @param[out] capabilities - OR-ed values of all supported HDR standards.  Please refer ::dsHDRStandard_t,
 *                                  dsHDRStandard_t is currently in the audioVisual combined file.
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 */
dsError_t dsGetHDRCapabilities(intptr_t handle, int *capabilities)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (capabilities == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    *capabilities = dsHDRSTANDARD_NONE;
    return dsERR_NONE;
}

/**
 * @brief Gets the video formats supported
 *
 * @param[in]   handle              - The handle returned from the dsGetVideoDevice() function
 * @param[out]  supported_formats   - OR-ed values of all the supported video codec formats.
 *                                           Please refer ::dsVideoCodingFormat_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 */
dsError_t dsGetSupportedVideoCodingFormats(intptr_t handle, unsigned int *supported_formats)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (supported_formats == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the video codec information
 *
 * @param[in]  handle   - The handle returned from the dsGetVideoDevice() function
 * @param[in]  codec    - OR-ed value of supported video codec formats.  Please refer ::dsVideoCodingFormat_t.
 * @param[out] info     - Structure containing Video codec information.  Please refer ::dsVideoCodecInfo_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 */
dsError_t dsGetVideoCodecInfo(intptr_t handle, dsVideoCodingFormat_t codec, dsVideoCodecInfo_t *info)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (info == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Disables, forcefully the HDR support of the device
 *
 * @param[in] handle    - The handle returned from the dsGetVideoDevice() function
 * @param[in] disable   - Boolean value to force disable HDR or not.
 *                              True to force disable, false to remove force disable
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 */
dsError_t dsForceDisableHDRSupport(intptr_t handle, bool disable)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the FRF mode of the device
 *
 * @param[in] handle    - The handle returned from the dsGetVideoDevice() function
 * @param[in] frfmode   - integer with corresponding Framerate value.
 *                               Please refer ::dsVideoFrameRate_t for max and min framerate.
 *
 * @return dsError_t                       - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsGetFRFMode()
 *
 */
dsError_t dsSetFRFMode(intptr_t handle, int frfmode)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || frfmode < 0)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the FRF mode of the device
 *
 * @param[in]  handle   - The handle returned from the dsGetVideoDevice() function
 * @param[out] frfmode  - integer with corresponding Framerate value of the device.
 *                             Please refer :: dsVideoFrameRate_t for max and min framerate.
 *
 * @return dsError_t                       - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsSetFRFMode()
 *
 */
dsError_t dsGetFRFMode(intptr_t handle, int *frfmode)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || frfmode == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the current framerate of the device
 *
 * @param[in]  handle       - The handle returned from the dsGetVideoDevice() function
 * @param[out] framerate    - Current frame rate will be represented in FPS
 *                             Please refer ::dsVideoFrameRate_t for  max and min framerate.
 *                            Updates the value as a string(eg:"60").
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsSetDisplayframerate()
 *
 */
dsError_t dsGetCurrentDisplayframerate(intptr_t handle, char *framerate)
{
    hal_info("invoked.\n");
    char *prt = NULL;
    char data[256] = {0};

    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || framerate == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    if (westerosRWWrapper("export XDG_RUNTIME_DIR=/run; westeros-gl-console get mode", data, sizeof(data))) {
        hal_info("data:'%s'\n", data);
        // Response: [0: mode 1280x720px60]
        char *start = strstr(data, "px");
        if (start) {
            start += 2;
            char *end = strchr(start, ']');
            if (end) {
                size_t length = end - start;
                strncpy(framerate, start, length);
                framerate[length] = '\0';
                hal_dbg("framerate: '%s'\n", framerate);
                return dsERR_NONE;
            } else {
                return dsERR_GENERAL;
            }
        } else {
            return dsERR_GENERAL;
        }
    }
    return dsERR_GENERAL;
}

/**
 * @brief Sets the display framerate for the device
 *
 * @param[in] handle    - The handle returned from the dsGetVideoDevice() function
 * @param[in] framerate - Framerate value to be set frame will be represented in FPS.
 *                        Please refer ::dsVideoFrameRate_t for  max and min framerate.
 *                        Expects the value as a string(eg:"60").
 *
 * @return dsError_t                       - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() and dsGetVideoDevice() must be called before calling this function
 *
 * @warning  This function is Not thread safe.
 *
 * @see dsGetCurrentDisplayframerate()
 *
 */
dsError_t dsSetDisplayframerate(intptr_t handle, char *framerate)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || framerate == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief This function is used to register the callback function for the Display framerate pre change event.
 *
 * @param[in] CBFunc    - Function callback to register for the event.
 *                              See dsRegisterFrameratePreChangeCB_t.
 *
 * @return dsError_t                       - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() must be called before calling this function.
 * @post dsRegisterFrameratePreChangeCB_t callback must be called after calling this function.
 *
 * @warning  This function is Not thread safe.
 */
dsError_t dsRegisterFrameratePreChangeCB(dsRegisterFrameratePreChangeCB_t CBFunc)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief This function is used to register a callback function for the Display framerate
 *                      post change event from the HAL side.
 *
 * @param[in] CBFunc    - Function to register for the event.
 *                                  See dsRegisterFrameratePostChangeCB_t.
 *
 * @return dsError_t                       - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoDeviceInit() must be called before calling this function.
 * @post dsRegisterFrameratePostChangeCB_t callback must be called after calling this function.
 *
 * @warning  This function is Not thread safe.
 *
 */
dsError_t dsRegisterFrameratePostChangeCB(dsRegisterFrameratePostChangeCB_t CBFunc)
{
    hal_info("invoked.\n");
    if (false == _bVideoDeviceInited)
    {
        return dsERR_NOT_INITIALIZED;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
