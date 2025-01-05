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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsTypes.h"
#include "dsVideoPort.h"
#include "dsVideoResolutionSettings.h"
#include "dsDisplay.h"
#include "dshalUtils.h"
#include "dshalLogger.h"

static bool isBootup = true;
static bool _bIsVideoPortInitialized = false;
static bool isValidVopHandle(intptr_t handle);
static const char *dsVideoGetResolution(uint32_t mode);
static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution);
#define MAX_HDMI_MODE_ID (127)

dsHDCPStatusCallback_t _halhdcpcallback = NULL;

typedef struct _VOPHandle_t
{
    dsVideoPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
    bool m_isEnabled;
} VOPHandle_t;

static VOPHandle_t _handles[dsVIDEOPORT_TYPE_MAX][2] = {};

static dsVideoPortResolution_t _resolution;

static void tvservice_hdcp_callback(void *callback_data,
                                    uint32_t reason,
                                    uint32_t param1,
                                    uint32_t param2)
{
    VOPHandle_t *hdmiHandle = (VOPHandle_t *)callback_data;
    hal_dbg("got reason %d\n", reason);
    switch (reason)
    {
    case VC_HDMI_HDCP_AUTH:
        _halhdcpcallback((int)(hdmiHandle->m_nativeHandle), dsHDCP_STATUS_AUTHENTICATED);
        break;

    case VC_HDMI_HDCP_UNAUTH:
        _halhdcpcallback((int)(hdmiHandle->m_nativeHandle), dsHDCP_STATUS_UNAUTHENTICATED);
        break;

    default:
    {
        if (isBootup == true)
        {
            hal_warn("%s: At bootup HDCP status is Authenticated for Rpi\n");
            _halhdcpcallback((int)(hdmiHandle->m_nativeHandle), dsHDCP_STATUS_AUTHENTICATED);
            isBootup = false;
        }
        break;
    }
    }
}

/**
 * @brief Register for a callback routine for HDCP Auth
 *
 * This function is used to register for a call back for HDCP Auth and unAuth events
 *
 * @param [in] handle   Handle for the video display
 * @param [out] *edid   The callback rouutine
 * @return dsError_t Error code.
 */

dsError_t dsRegisterHdcpStatusCallback(intptr_t handle, dsHDCPStatusCallback_t cb)
{
    dsError_t ret = dsERR_NONE;
    /* Register The call Back */
    _halhdcpcallback = cb;
    return ret;
}

/**
 * @brief Initializes the underlying Video Port sub-system.
 *
 * This function must initialize all the video specific output ports and any associated resources.
 *
 * @return dsError_t                    - Status
 * @retval dsERR_NONE                   - Success
 * @retval dsERR_ALREADY_INITIALIZED    - Function is already initialized
 * @retval dsERR_RESOURCE_NOT_AVAILABLE - Resources have failed to allocate
 * @retval dsERR_GENERAL                - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsVideoPortTerm()
 */
dsError_t dsVideoPortInit()
{
    hal_dbg("invoked.\n");
    if (true == _bIsVideoPortInitialized)
    {
        return dsERR_ALREADY_INITIALIZED;
    }

    _handles[dsVIDEOPORT_TYPE_HDMI][0].m_vType = dsVIDEOPORT_TYPE_HDMI;
    _handles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = dsVIDEOPORT_TYPE_HDMI;
    _handles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;
    _handles[dsVIDEOPORT_TYPE_HDMI][0].m_isEnabled = true;

    _resolution = kResolutions[kDefaultResIndex];
    int rc = vchi_tv_init();
    if (rc != 0)
    {
        hal_err("Failed to initialise tv service\n");
        return dsERR_GENERAL;
    }

    // Register HDCP callback
    vc_tv_register_callback(&tvservice_hdcp_callback, &_handles[dsVIDEOPORT_TYPE_HDMI][0]);
    _bIsVideoPortInitialized = true;

    return dsERR_NONE;
}

/**
 * @brief Gets the handle for video port requested
 *
 * This function is used to get the handle of the video port corresponding to specified port type. It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the requested video port is unavailable.
 *
 * @param[in]  type     - Type of video port (e.g. HDMI).  Please refer ::dsVideoPortType_t
 * @param[in]  index    - Index of the video device (0, 1, ...)  (Index of the port must be 0 if not specified)
 *                          Max index is platform specific. Min value is 0.   Please refer ::kSupportedPortTypes
 * @param[out] handle   - The handle used by the Caller to uniquely identify the HAL instance
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre dsVideoPortInit() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetVideoPort(dsVideoPortType_t type, int index, intptr_t *handle)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (index != 0 || !dsVideoPortType_isValid(type) || NULL == handle)
    {
        return dsERR_INVALID_PARAM;
    }

    /* Report only HDMI OUT is supported. */
    if (type != dsVIDEOPORT_TYPE_HDMI)
    {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    *handle = (intptr_t)&_handles[type][index];

    return dsERR_NONE;
}
/**
 * @brief Enable/disable all video port.
 *
 * This function enables or disables the all video port.
 *
 * @param [in] enabled     Flag to control the video port state
 *                         (@a true to enable, @a false to disable)
 * @return Device Settings error code
 * @retval dsERR_NONE If sucessfully dsEnableAllVideoPort api has been called using IARM support.
 * @retval dsERR_GENERAL General failure.
 */
dsError_t dsEnableAllVideoPort(bool enabled)
{
    hal_dbg("invoked.\n");
    /* We cannot enable all ports in raspberrypi  because by default other
     * port will be disabled when we enable any videoport on rpi */
    return dsERR_NONE;
}

/**
 * @brief Checks whether a video port is enabled or not.
 *
 * This function indicates whether the specified video port is enabled or not.
 *
 * @param[in]  handle   - Handle of the video port returned from dsGetVideoPort()
 * @param[out] enabled  - Flag to hold the enabled status of Video Port.
 *                          ( @a true when video port is enabled or @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableVideoPort()
 */
dsError_t dsIsVideoPortEnabled(intptr_t handle, bool *enabled)
{
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == enabled)
    {
        return dsERR_INVALID_PARAM;
    }

    *enabled = false;
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        *enabled = vopHandle->m_isEnabled;
    }
    else
    {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    return dsERR_NONE;
}

/**
 * @brief Enables/Disables a video port.
 *
 * This function enables or disables the specified video port.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 * @param[in] enabled   - Flag to enable/disable the video port
 *                         ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsVideoPortEnabled()
 */
dsError_t dsEnableVideoPort(intptr_t handle, bool enabled)
{
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    int res = 0, rc = 0;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        if (enabled != vopHandle->m_isEnabled)
        {
            if (enabled)
            {
                res = vc_tv_hdmi_power_on_preferred();
                if (res != 0)
                {
                    hal_err("Failed to power on HDMI with preferred settings\n");
                    return dsERR_GENERAL;
                }
                rc = system("/lib/rdk/rpiDisplayEnable.sh 1");
                if (rc == -1)
                {
                    hal_err("Failed to run script rpiDisplayEnable.sh with enable=1 rc=%d\n", rc);
                    return dsERR_GENERAL;
                }
            }
            else
            {
                rc = system("/lib/rdk/rpiDisplayEnable.sh 0");
                if (rc == -1)
                {
                    hal_err("Failed to run script rpiDisplayEnable.sh with enable=0 rc=%d \n", rc);
                    return dsERR_GENERAL;
                }
                sleep(1);

                res = vc_tv_power_off();
                if (res != 0)
                {
                    hal_err("Failed to power off HDMI\n");
                    return dsERR_GENERAL;
                }
            }
        }
        vopHandle->m_isEnabled = enabled;
    }
    else
    {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Checks whether the specific video port is connected to display.
 *
 * This function is used to check whether video port is connected to a display or not.
 *
 * @param[in]  handle       - Handle of the video port returned from dsGetVideoPort()
 * @param[out] connected    - Flag to hold the connection status of display
 *                              ( @a true if display is connected or @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsDisplayConnected(intptr_t handle, bool *connected)
{
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    TV_DISPLAY_STATE_T tvstate;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == connected)
    {
        return dsERR_INVALID_PARAM;
    }
    /*Default is false*/
    *connected = false;

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        hal_dbg("dsVIDEOPORT_TYPE_HDMI port\n");
        if (vc_tv_get_display_state(&tvstate) == 0)
        {
            hal_dbg("vc_tv_get_display_state: 0x%x\n", tvstate.state);
            if (tvstate.state & VC_HDMI_ATTACHED)
            {
                *connected = true;
            }
            else if (tvstate.state & VC_HDMI_UNPLUGGED)
            {
                *connected = false;
            }
            else
            {
                hal_err("Cannot find HDMI state\n");
                return dsERR_GENERAL;
            }
        }
        else
        {
            hal_err("vc_tv_get_display_state failed\n");
            return dsERR_GENERAL;
        }
    }
    else
    {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Enables/Disables the DTCP of a video port.
 *
 * This function is used to enable/disable the DTCP (Digital Transmission Content Protection)
 * for the specified video port. It must return dsERR_OPERATION_NOT_SUPPORTED if connected
 * video port does not support DTCP.
 *
 *
 * @param[in] handle            - Handle of the video port returned from dsGetVideoPort()
 * @param[in] contentProtect    - Flag to enable/disable DTCP content protection
 *                               ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsDTCPEnabled()
 */
dsError_t dsEnableDTCP(intptr_t handle, bool contentProtect)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables/Disables the HDCP of a video port.
 *
 * This function is used to enable/disable the HDCP (High-bandwidth Digital Content Protection)
 * for the specified video port. It must return dsERR_OPERATION_NOT_SUPPORTED if connected
 * video port does not support HDCP.
 *
 * @param[in] handle            - Handle of the video port returned from dsGetVideoPort()
 * @param[in] contentProtect    - Flag to enable/disable DTCP content protection
 *                                  ( @a true to enable, @a false to disable)
 * @param[in] hdcpKey           - HDCP key
 * @param[in] keySize           - HDCP key size.  Please refer ::HDCP_KEY_MAX_SIZE
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetHDCPStatus(), dsIsHDCPEnabled()
 */
dsError_t dsEnableHDCP(intptr_t handle, bool contentProtect, char *hdcpKey, size_t keySize)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == hdcpKey || keySize >= HDCP_KEY_MAX_SIZE)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Indicates whether a video port is DTCP protected.
 *
 * This function indicates whether the specified video port is configured for DTCP
 * content protection. It must return dsERR_OPERATION_NOT_SUPPORTED if DTCP
 * is not supported.
 *
 * @param[in]  handle               - Handle of the video port returned from dsGetVideoPort()
 * @param [out] pContentProtected   - Current DTCP content protection status
 *                                      ( @a true when enabled, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableDTCP()
 */
dsError_t dsIsDTCPEnabled(intptr_t handle, bool *pContentProtected)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == pContentProtected)
    {
        return dsERR_INVALID_PARAM;
    }
    *pContentProtected = false;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Indicates whether a video port is HDCP protected.
 *
 * This function indicates whether the specified video port is configured for HDCP
 * content protection. It must return dsERR_OPERATION_NOT_SUPPORTED if HDCP
 * is not supported.
 *
 * @param[in]  handle               - Handle of the video port returned from dsGetVideoPort()
 * @param [out] pContentProtected   - Current HDCP content protection status
 *                                      ( @a true when enabled, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsIsHDCPEnabled(intptr_t handle, bool *pContentProtected)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == pContentProtected)
    {
        return dsERR_INVALID_PARAM;
    }
    *pContentProtected = false;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the display resolution of specified video port.
 *
 * This function gets the current display resolution of the specified video port.
 *
 * @param[in] handle        - Handle of the video port returned from dsGetVideoPort()
 * @param [out] resolution  - Current resolution of the video port.  Please refer ::dsVideoPortResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetResolution()
 */
dsError_t dsGetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
{
    hal_dbg("invoked.\n");
    const char *resolution_name = NULL;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == resolution)
    {
        return dsERR_INVALID_PARAM;
    }
    TV_DISPLAY_STATE_T tvstate;
    if (vc_tv_get_display_state(&tvstate) == 0)
    {
        hal_dbg("vc_tv_get_display_state: 0x%X\n", tvstate.state);
        if (tvstate.state & VC_HDMI_ATTACHED)
        {
            printf("  Width: %d\n", tvstate.display.hdmi.width);
            printf("  Height: %d\n", tvstate.display.hdmi.height);
            printf("  Frame Rate: %d\n", tvstate.display.hdmi.frame_rate);
            printf("  Scan Mode: %s\n", tvstate.display.hdmi.scan_mode ? "Interlaced" : "Progressive");
            printf("  Aspect Ratio: %d\n", tvstate.display.hdmi.aspect_ratio);
            printf("  Pixel Clock: %d\n", tvstate.display.hdmi.pixel_freq);
            printf("  Group: %d\n", tvstate.display.hdmi.group);
            printf("  Mode: %d\n", tvstate.display.hdmi.mode);
            printf("  3D Structure Mask: 0x%X\n", tvstate.display.hdmi.struct_3d_mask);
            resolution_name = dsVideoGetResolution(tvstate.display.hdmi.mode);
        }
        else
        {
            hal_err("HDMI not connected.\n");
            return dsERR_GENERAL;
        }
    }
    else
    {
        hal_err("Failed vc_tv_hdmi_get_display_state.\n");
        return dsERR_GENERAL;
    }
    // if (resolution_name == NULL) {
    //  	hdmi_mode = dsGetHdmiMode(resolution);
    //  	resolution_name = dsVideoGetResolution(hdmi_mode);
    // }
    if (resolution_name)
        strncpy(resolution->name, resolution_name, strlen(resolution_name));
    return dsERR_NONE;
}

// TODO: refactor with  proper error return.
static const char *dsVideoGetResolution(uint32_t hdmiMode)
{
    hal_dbg("invoked.\n");
    const char *res_name = NULL;
    size_t iCount = (sizeof(resolutionMap) / sizeof(resolutionMap[0]));
    for (size_t i = 0; i < iCount; i++)
    {
        if (resolutionMap[i].mode == (int)hdmiMode)
            res_name = resolutionMap[i].rdkRes;
    }
    return res_name;
}

static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution)
{
    hal_dbg("invoked.\n");
    uint32_t hdmi_mode = 0;
    size_t iCount = (sizeof(resolutionMap) / sizeof(resolutionMap[0]));
    for (size_t i = 0; i < iCount; i++)
    {
        size_t length = strlen(resolution->name) > strlen(resolutionMap[i].rdkRes) ? strlen(resolution->name) : strlen(resolutionMap[i].rdkRes);
        if (!strncmp(resolution->name, resolutionMap[i].rdkRes, length))
        {
            hdmi_mode = resolutionMap[i].mode;
            break;
        }
    }
    if (!hdmi_mode)
    {
        hal_dbg("Given resolution not found, setting default Resolution HDMI_CEA_720p60\n");
        hdmi_mode = HDMI_CEA_720p60;
    }
    return hdmi_mode;
}

/**
 * @brief Sets the display resolution of specified video port.
 *
 * This function sets the resolution of the specified video port.
 *
 * @param[in] handle        - Handle of the video port returned from dsGetVideoPort()
 * @param[in] resolution    - Video resolution. Please refer ::dsVideoPortResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetResolution()
 */
dsError_t dsSetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
{
    /* Auto Select uses 720p. Should be converted to dsVideoPortResolution_t = 720p in DS-VOPConfig, not here */
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    int res = 0;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == resolution)
    {
        return dsERR_INVALID_PARAM;
    }
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        hal_dbg("Setting HDMI resolution '%s'\n", resolution->name);
        uint32_t hdmi_mode;
        hdmi_mode = dsGetHdmiMode(resolution);
        res = vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI, HDMI_RES_GROUP_CEA, hdmi_mode);
        if (res != 0)
        {
            hal_err("Failed to set HDMI resolution\n");
            return dsERR_GENERAL;
        }
        sleep(1);
        system("fbset -depth 16");
        system("fbset -depth 32");
    }
    else
    {
        hal_err("Video port type not supported\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Terminates the underlying Video Port sub-system.
 *
 * This function must terminate all the video output ports and any associated resources.
 *
 * @return dsError_t                - Status
 * @retval dsERR_NONE               - Success
 * @retval dsERR_NOT_INITIALIZED    - Module is not initialised
 * @retval dsERR_GENERAL            - Underlying undefined platform error
 *
 * @pre dsVideoPortInit() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsVideoPortInit()
 */
dsError_t dsVideoPortTerm()
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    vchi_tv_uninit();
    _bIsVideoPortInitialized = false;
    return dsERR_NONE;
}

/**
 * @brief To check whether the handle is valid or not
 *
 * This function will be used to validate the  handles that are given
 *
 * @param [in] handle  Handle for the Output Audio port
 * @return bool  true for valid handle
 */
static bool isValidVopHandle(intptr_t m_handle)
{
    for (int i = 0; i < dsVIDEOPORT_TYPE_MAX; i++)
    {
        if ((intptr_t)&_handles[i][0] == m_handle)
        {
            return true;
        }
    }
    return false;
}

/**
 * @brief Checks whether a video port is active or not.
 *
 * This function is used to indicate whether a video port is active or not. A HDMI output port is active if it is connected
 * to the active port of sink device.
 *
 * @param[in]  handle   - Handle of the video port returned from dsGetVideoPort()
 * @param[out] active   - Connection state of the video port
 *                          ( @a true if connected, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @see dsSetActiveSource()
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsVideoPortActive(intptr_t handle, bool *active)
{
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    TV_DISPLAY_STATE_T tvstate;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == active)
    {
        return dsERR_INVALID_PARAM;
    }
    /* Default to false */
    *active = false;

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        if (vc_tv_get_display_state(&tvstate) == 0)
        {
            hal_dbg("vc_tv_get_display_state: 0x%x\n", tvstate.state);
            if (tvstate.state & VC_HDMI_HDMI)
                *active = true;
            else if (tvstate.state & VC_HDMI_UNPLUGGED)
                *active = false;
            else
                hal_warn("%s: Cannot find HDMI state\n");
        }
        else
        {
            hal_err("vc_tv_get_display_state failed\n");
            return dsERR_GENERAL;
        }
    }
    else
    {
        hal_err("Video port type not supported\n");
        return dsERR_INVALID_PARAM;
    }
    return dsERR_NONE;
}

/**
 * @brief Gets the HDCP protocol version of the device.
 *
 * @param[in] handle                - Handle of the video port returned from dsGetVideoPort()
 * @param [out] protocolVersion     - HDCP protocol version.  Please refer ::dsHdcpProtocolVersion_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsGetHDCPProtocol(intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDCPReceiverProtocol(intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDCPCurrentProtocol(intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetTVHDRCapabilities(intptr_t handle, int *capabilities)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == capabilities)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported resolutions of TV.
 *
 * This function is used to get TV supported resolutions of TV/display device.
 *
 * @param[in] handle            - Handle of the video port(TV) returned from dsGetVideoPort()
 * @param [out] resolutions     - OR-ed value supported resolutions.  Please refer ::dsTVResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsSupportedTvResolutions(intptr_t handle, int *resolutions)
{
    hal_dbg("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (resolutions != NULL && isValidVopHandle(handle) && vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
    {
        TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_MODE_ID];
        HDMI_RES_GROUP_T group;
        uint32_t mode;
        int num_of_modes;
        int i;
        num_of_modes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, modeSupported,
                                                          vcos_countof(modeSupported),
                                                          &group,
                                                          &mode);
        if (num_of_modes <= 0)
        {
            hal_err("vc_tv_hdmi_get_supported_modes_new() error, Failed to get modes\n");
            return dsERR_GENERAL;
        }
        for (i = 0; i < num_of_modes; i++)
        {
            hal_dbg("Supported HDMI mode: %u\n", modeSupported[i].code);
            switch (modeSupported[i].code)
            {
            case HDMI_CEA_480p60:
            case HDMI_CEA_480p60H:
            case HDMI_CEA_480p60_2x:
            case HDMI_CEA_480p60_2xH:
            case HDMI_CEA_480p60_4x:
            case HDMI_CEA_480p60_4xH:
                *resolutions |= dsTV_RESOLUTION_480p;
                break;
            case HDMI_CEA_480i60:
            case HDMI_CEA_480i60H:
            case HDMI_CEA_480i60_4x:
            case HDMI_CEA_480i60_4xH:
                *resolutions |= dsTV_RESOLUTION_480i;
                break;
            case HDMI_CEA_576i50:
            case HDMI_CEA_576i50H:
            case HDMI_CEA_576i50_4x:
            case HDMI_CEA_576i50_4xH:
                *resolutions |= dsTV_RESOLUTION_576i;
                break;
            case HDMI_CEA_576p50:
            case HDMI_CEA_576p50H:
            case HDMI_CEA_576p50_2x:
            case HDMI_CEA_576p50_2xH:
            case HDMI_CEA_576p50_4x:
            case HDMI_CEA_576p50_4xH:
                *resolutions |= dsTV_RESOLUTION_576p50;
                break;
            case HDMI_CEA_720p50:
                *resolutions |= dsTV_RESOLUTION_720p50;
                break;
            case HDMI_CEA_720p60:
                *resolutions |= dsTV_RESOLUTION_720p;
                break;
            case HDMI_CEA_1080p50:
                *resolutions |= dsTV_RESOLUTION_1080p50;
                break;
            case HDMI_CEA_1080p24:
                *resolutions |= dsTV_RESOLUTION_1080p24;
                break;
            case HDMI_CEA_1080p25:
                *resolutions |= dsTV_RESOLUTION_1080p25;
                break;
            case HDMI_CEA_1080p30:
                *resolutions |= dsTV_RESOLUTION_1080p30;
                break;
            case HDMI_CEA_1080p60:
                *resolutions |= dsTV_RESOLUTION_1080p60;
                break;
            case HDMI_CEA_1080i50:
                *resolutions |= dsTV_RESOLUTION_1080i50;
                break;
            case HDMI_CEA_1080i60:
                *resolutions |= dsTV_RESOLUTION_1080i;
                break;
            default:
                *resolutions |= dsTV_RESOLUTION_480p;
                break;
            }
        }
    }
    else
    {
        hal_err("Error:Get supported resolution for TV on Non HDMI Port\n");
        return dsERR_INVALID_PARAM;
    }
    hal_dbg("Supported resolutions: 0x%x\n", *resolutions);
    return dsERR_NONE;
}

/**
 * @brief Checks if the connected display supports the audio surround.
 *
 * This function is used to check if the display connected to video port supports the audio surround.
 *
 * @param[in]  handle   - Handle of the video port returned from dsGetVideoPort()
 * @param[out] surround - Audio surround support  ( @a true if display supports surround sound or @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsDisplaySurround(intptr_t handle, bool *surround)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || surround == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    // TODO: RPI4 does support this feature; implement later.
    /* config.txt with the following
     * hdmi_group=1
     * hdmi_mode=16
     * hdmi_drive=2
     * hdmi_force_hotplug=1 (optional)
     * Check the current audion output: amixer cget numid=3
     * Set the audio output to HDMI: amixer cset numid=3 2
     */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the surround mode of video port
 *
 * This function is used to get the surround mode of the specified video port.
 *
 * @param[in]  handle   - Handle of the video port returned from dsGetVideoPort()
 * @param[out] surround - Surround mode .Please refer :: dsSURROUNDMode_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetSurroundMode(intptr_t handle, int *surround)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || surround == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsVideoFormatUpdateRegisterCB(dsVideoFormatUpdateCB_t cb)
{
    hal_dbg("invoked.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the specified video port as active source.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @see dsIsVideoPortActive()
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsSetActiveSource(intptr_t handle)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
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
 * @brief Gets the current HDCP status of the specified video port.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 * @param[out] status   - HDCP status of the video port.  Please refer ::dsHdcpStatus_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsGetHDCPStatus(intptr_t handle, dsHdcpStatus_t *status)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (status == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetForceDisable4KSupport(intptr_t handle, bool disable)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetForceDisable4KSupport(intptr_t handle, bool *disable)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || disable == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetVideoEOTF(intptr_t handle, dsHDRStandard_t *video_eotf)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (video_eotf == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetMatrixCoefficients(intptr_t handle, dsDisplayMatrixCoefficients_t *matrix_coefficients)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (matrix_coefficients == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetColorDepth(intptr_t handle, unsigned int *color_depth)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (color_depth == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetColorSpace(intptr_t handle, dsDisplayColorSpace_t *color_space)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (color_space == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetQuantizationRange(intptr_t handle, dsDisplayQuantizationRange_t *quantization_range)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (quantization_range == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetCurrentOutputSettings(intptr_t handle, dsHDRStandard_t *video_eotf, dsDisplayMatrixCoefficients_t *matrix_coefficients, dsDisplayColorSpace_t *color_space, unsigned int *color_depth, dsDisplayQuantizationRange_t *quantization_range)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (video_eotf == NULL || matrix_coefficients == NULL || color_space == NULL || color_depth == NULL || quantization_range == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsIsOutputHDR(intptr_t handle, bool *hdr)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdr == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsResetOutputToSDR()
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetHdmiPreference(intptr_t handle, dsHdcpProtocolVersion_t *hdcpCurrentProtocol)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdcpCurrentProtocol == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHdmiPreference(intptr_t handle, dsHdcpProtocolVersion_t *hdcpCurrentProtocol)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdcpCurrentProtocol == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetIgnoreEDIDStatus(intptr_t handle, bool *status)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle) || status == NULL)
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetBackgroundColor(intptr_t handle, dsVideoBackgroundColor_t color)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetForceHDRMode(intptr_t handle, dsHDRStandard_t mode)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsColorDepthCapabilities(intptr_t handle, unsigned int *colorDepthCapability)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepthCapability == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t *colorDepth)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepth == NULL || !dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t colorDepth)
{
    hal_dbg("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidHandle(handle))
    {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
