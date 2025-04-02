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
#include "interface/vmcs_host/vc_hdmi.h"

static bool isBootup = true;
static bool _bIsVideoPortInitialized = false;
static bool isValidVopHandle(intptr_t handle);
//static const char *dsVideoGetResolution(uint32_t mode);
//static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution);
#define MAX_HDMI_MODE_ID (127)

dsHDCPStatusCallback_t _halhdcpcallback = NULL;

typedef struct _VOPHandle_t {
    dsVideoPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
    bool m_isEnabled;
} VOPHandle_t;

static VOPHandle_t _vopHandles[dsVIDEOPORT_TYPE_MAX][2] = {};

static dsVideoPortResolution_t _resolution;

static void tvservice_hdcp_callback(void *callback_data,
        uint32_t reason, uint32_t param1, uint32_t param2)
{
    VOPHandle_t *hdmiHandle = (VOPHandle_t *)callback_data;
    hal_info("Got handle %p with reason %d, param1 %d, param2 %d\n",
            hdmiHandle, reason, param1, param2);
    switch (reason) {
        case VC_HDMI_HDCP_AUTH:
            if (NULL != _halhdcpcallback) {
                _halhdcpcallback((int)(hdmiHandle->m_nativeHandle),
                        dsHDCP_STATUS_AUTHENTICATED);
            } else {
                hal_warn("_halhdcpcallback is Null.\n");
            }
            break;
        case VC_HDMI_HDCP_UNAUTH:
            if (NULL != _halhdcpcallback) {
                _halhdcpcallback((int)(hdmiHandle->m_nativeHandle),
                        dsHDCP_STATUS_UNAUTHENTICATED);
            } else {
                hal_warn("_halhdcpcallback is Null.\n");
            }
            break;
        case VC_HDMI_HDCP_KEY_DOWNLOAD:
        case VC_HDMI_HDCP_SRM_DOWNLOAD:
            hal_warn("Dropping event, VC_HDMI_HDCP_KEY_DOWNLOAD/VC_HDMI_HDCP_SRM_DOWNLOAD\n.");
            break;
        default:
            if (isBootup == true) {
                hal_warn("At bootup HDCP status is Authenticated for Rpi\n");
                if (NULL != _halhdcpcallback) {
                    _halhdcpcallback((int)(hdmiHandle->m_nativeHandle), dsHDCP_STATUS_AUTHENTICATED);
                } else {
                    hal_warn("_halhdcpcallback is Null.\n");
                }
                isBootup = false;
            }
            break;
    }
}

/**
 * @brief Register for a callback routine for HDCP Auth
 *
 * This function is used to register for a call back for HDCP Auth and unAuth
 * events
 *
 * @param [in] handle   Handle for the video display
 * @param [out] *edid   The callback rouutine
 * @return dsError_t Error code.
 */

dsError_t dsRegisterHdcpStatusCallback(intptr_t handle, dsHDCPStatusCallback_t cb)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if ((access("/tmp/.accept_null_handle_for_cbs", F_OK) == -1 && !isValidVopHandle(handle)) || NULL == cb) {
        hal_err("handle(%p) is invalid or cb(%p) is null.\n", handle, cb);
        return dsERR_INVALID_PARAM;
    }
    /* Register The call Back */
    if (NULL != _halhdcpcallback) {
        hal_warn("HDCP callback is already registered; overriding with new one.\n");
    }
    _halhdcpcallback = cb;
    return dsERR_NONE;
}

/**
 * @brief Initializes the underlying Video Port sub-system.
 *
 * This function must initialize all the video specific output ports and any
 * associated resources.
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
dsError_t  dsVideoPortInit()
{
    hal_info("invoked.\n");
    if (true == _bIsVideoPortInitialized) {
        return dsERR_ALREADY_INITIALIZED;
    }
    /*
     * Video Port configuration for HDMI and Analog ports.
     */

    _vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType = dsVIDEOPORT_TYPE_HDMI;
    _vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = dsVIDEOPORT_TYPE_HDMI;
    _vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;
    _vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_isEnabled = true;

    hal_info("&_vopHandles = %p\n", &_vopHandles);
    hal_info("&_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType = %p\n", &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType);
    hal_info("&_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = %p\n", &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle);
    hal_info("&_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index = %p\n", &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index);
    hal_info("&_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_isEnabled = %p\n", &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0].m_isEnabled);
    _resolution = kResolutions[kDefaultResIndex];
    int rc = vchi_tv_init();
    if (rc != 0) {
        hal_err("Failed to initialise tv service\n");
        return dsERR_GENERAL;
    }
    /*
     *  Register callback for HDCP Auth
     */
    vc_tv_register_callback(&tvservice_hdcp_callback, &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0]);

    _bIsVideoPortInitialized = true;

    return dsERR_NONE;
}

/**
 * @brief Gets the handle for video port requested
 *
 * This function is used to get the handle of the video port corresponding to
 * specified port type. It must return dsERR_OPERATION_NOT_SUPPORTED if the
 * requested video port is unavailable.
 *
 * @param[in]  type     - Type of video port (e.g. HDMI).  Please refer
 * ::dsVideoPortType_t
 * @param[in]  index    - Index of the video device (0, 1, ...)  (Index of the
 * port must be 0 if not specified) Max index is platform specific. Min value is
 * 0.   Please refer ::kSupportedPortTypes
 * @param[out] handle   - The handle used by the Caller to uniquely identify the
 * HAL instance
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialized
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre dsVideoPortInit() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */

dsError_t dsGetVideoPort(dsVideoPortType_t type, int index, intptr_t *handle)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (index != 0 || !dsVideoPortType_isValid(type) || NULL == handle) {
        hal_err("index = %d, type = %d, handle = %p\n", index, type, handle);
        return dsERR_INVALID_PARAM;
    }
    *handle = (intptr_t)&_vopHandles[type][index];
    hal_dbg("*handle = %p\n", *handle);
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
 * @retval dsERR_NONE If sucessfully dsEnableAllVideoPort api has been called
 * using IARM support.
 * @retval dsERR_GENERAL General failure.
 */
dsError_t dsEnableAllVideoPort(bool enabled)
{
    hal_info("invoked with enabled %d\n", enabled);
    /* We cannot enable all ports in raspberrypi  because by default other
     * port will be disabled when we enable any videoport on rpi */
    return dsERR_NONE;
}

/**
 * @brief Checks whether a video port is enabled or not.
 *
 * This function indicates whether the specified video port is enabled or not.
 *
 * @param[in]  handle   - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] enabled  - Flag to hold the enabled status of Video Port.
 *                          ( @a true when video port is enabled or @a false
 * otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableVideoPort()
 */
dsError_t dsIsVideoPortEnabled(intptr_t handle, bool *enabled)
{
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == enabled)
    {
        hal_err("handle(%p) is invalid or enabled(%p) is null.\n", handle, enabled);
        return dsERR_INVALID_PARAM;
    }
    if(vopHandle->m_vType == dsVIDEOPORT_TYPE_COMPONENT)
    {
        *enabled = vopHandle->m_isEnabled;
    }
    else if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI)
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
 * @param[in] handle    - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[in] enabled   - Flag to enable/disable the video port
 *                         ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsVideoPortEnabled()
 */
dsError_t dsEnableVideoPort(intptr_t handle, bool enabled)
{
    hal_info("invoked.\n");

    if (!_bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid.\n", handle);
        return dsERR_INVALID_PARAM;
    }

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    int res = 0;

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        char cmd[128] = {0};
        char resp[128] = {0};
		snprintf(cmd, sizeof(cmd), "export XDG_RUNTIME_DIR=/run; westeros-gl-console set display enable %d", (enabled ? 1 : 0));

		if (enabled) {
			res = vc_tv_hdmi_power_on_preferred();
			if (res != 0) {
				hal_err("Failed to power on HDMI with preferred settings\n");
				return dsERR_GENERAL;
			}
		}

		if (!westerosRWWrapper(cmd, resp, sizeof(resp))) {
			hal_err("Failed to run '%s', got response '%s'\n", cmd, resp);
			return dsERR_GENERAL;
		}

		if (!enabled) {
			sleep(1);
			res = vc_tv_power_off();
			if (res != 0) {
				hal_err("Failed to disable HDMI video port\n");
				return dsERR_GENERAL;
			}
		}
    } else {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    vopHandle->m_isEnabled = enabled;
    return dsERR_NONE;
}

/**
 * @brief Checks whether the specific video port is connected to display.
 *
 * This function is used to check whether video port is connected to a display
 * or not.
 *
 * @param[in]  handle       - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] connected    - Flag to hold the connection status of display
 *                              ( @a true if display is connected or @a false
 * otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsDisplayConnected(intptr_t handle, bool *connected)
{
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    TV_DISPLAY_STATE_T tvstate;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == connected) {
        hal_err("handle(%p) is invalid or connected(%p) is null.\n", handle, connected);
        return dsERR_INVALID_PARAM;
    }

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_BB) {
        hal_warn("Handle is dsVIDEOPORT_TYPE_BB, return not supported\n");
		return dsERR_OPERATION_NOT_SUPPORTED;
    }
	/*Default is false*/
    *connected = false;
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        hal_dbg("Isdisplayconnected HDMI port\n");
        if (vc_tv_get_display_state(&tvstate) == 0) {
            hal_dbg("vc_tv_get_display_state: 0x%x\n", tvstate.state);
            if (tvstate.state & VC_HDMI_ATTACHED) {
                hal_dbg("HDMI is connected\n");
                *connected = true;
            } else if (tvstate.state & VC_HDMI_UNPLUGGED) {
                hal_dbg("HDMI is not connected\n");
                *connected = false;
            } else {
                hal_err("Cannot find HDMI state\n");
                return dsERR_GENERAL;
            }
        }
    } else {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Enables/Disables the DTCP of a video port.
 *
 * This function is used to enable/disable the DTCP (Digital Transmission
 * Content Protection) for the specified video port. It must return
 * dsERR_OPERATION_NOT_SUPPORTED if connected video port does not support DTCP.
 *
 *
 * @param[in] handle            - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[in] contentProtect    - Flag to enable/disable DTCP content protection
 *                               ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsDTCPEnabled()
 */
dsError_t dsEnableDTCP(intptr_t handle, bool contentProtect)
{
    hal_info("invoked with contentProtect %d.\n", contentProtect);
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle))
    {
        hal_err("handle(%p) is invalid.\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables/Disables the HDCP of a video port.
 *
 * This function is used to enable/disable the HDCP (High-bandwidth Digital
 * Content Protection) for the specified video port. It must return
 * dsERR_OPERATION_NOT_SUPPORTED if connected video port does not support HDCP.
 *
 * @param[in] handle            - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[in] contentProtect    - Flag to enable/disable DTCP content protection
 *                                  ( @a true to enable, @a false to disable)
 * @param[in] hdcpKey           - HDCP key
 * @param[in] keySize           - HDCP key size.  Please refer
 * ::HDCP_KEY_MAX_SIZE
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetHDCPStatus(), dsIsHDCPEnabled()
 */
dsError_t dsEnableHDCP(intptr_t handle, bool contentProtect, char *hdcpKey, size_t keySize)
{
    // Ref: https://forums.raspberrypi.com/viewtopic.php?t=278193
    hal_info("invoked with contentProtect %d\n", contentProtect);
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == hdcpKey || keySize >= HDCP_KEY_MAX_SIZE) {
        hal_err("handle(%p) is invalid or hdcpkey is NULL.\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Indicates whether a video port is DTCP protected.
 *
 * This function indicates whether the specified video port is configured for
 * DTCP content protection. It must return dsERR_OPERATION_NOT_SUPPORTED if DTCP
 * is not supported.
 *
 * @param[in]  handle               - Handle of the video port returned from
 * dsGetVideoPort()
 * @param [out] pContentProtected   - Current DTCP content protection status
 *                                      ( @a true when enabled, @a false
 * otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableDTCP()
 */dsError_t dsIsDTCPEnabled(intptr_t handle, bool *pContentProtected)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == pContentProtected)
    {
        hal_err("handle(%p) is invalid or pContentProtected(%p) is NULL.\n", handle, pContentProtected);
        return dsERR_INVALID_PARAM;
    }
    *pContentProtected = false;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Indicates whether a video port is HDCP protected.
 *
 * This function indicates whether the specified video port is configured for
 * HDCP content protection. It must return dsERR_OPERATION_NOT_SUPPORTED if HDCP
 * is not supported.
 *
 * @param[in]  handle               - Handle of the video port returned from
 * dsGetVideoPort()
 * @param [out] pContentProtected   - Current HDCP content protection status
 *                                      ( @a true when enabled, @a false
 * otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsIsHDCPEnabled(intptr_t handle, bool *pContentProtected)
{
    // Ref: https://forums.raspberrypi.com/viewtopic.php?t=278193
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == pContentProtected){
        hal_err("handle(%p) is invalid or pContentProtected(%p) is NULL.\n", handle, pContentProtected);
        return dsERR_INVALID_PARAM;
    }
    *pContentProtected = false;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the display resolution of specified video port.
 *
 * This function gets the current display resolution of the specified video
 * port.
 *
 * @param[in] handle        - Handle of the video port returned from
 * dsGetVideoPort()
 * @param [out] resolution  - Current resolution of the video port.  Please
 * refer ::dsVideoPortResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetResolution()
 */
dsError_t dsGetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
{
    hal_info("invoked.\n");
    TV_DISPLAY_STATE_T tvstate;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == resolution) {
        hal_err("handle(%p) is invalid or resolution(%p) is NULL.\n", handle, resolution);
        return dsERR_INVALID_PARAM;
    }
	hal_info("Using westeros for resolution\n");
	char data[128] = {0};
	if (westerosRWWrapper("export XDG_RUNTIME_DIR=/run; westeros-gl-console get mode", data, sizeof(data))) {
		char wstresolution[64] = {0};
		hal_info("data:'%s'\n", data);
		// Response: [0: mode 1280x720px60]
		// Extract string between 'mode ' and ']' and store in resolution
		char *start = strstr(data, "mode ");
		if (start) {
			start += strlen("mode ");
			char *end = strstr(start, "]");
			if (end) {
				*end = '\0';
				strncpy(wstresolution, start, sizeof(wstresolution));
				hal_info("Resolution string: '%s'\n", wstresolution);
				if (convertWesterosResolutionTokResolution(wstresolution, resolution)) {
					hal_dbg("Leena name: '%s'\n", resolution->name);
					hal_dbg("Leena pixelResolution: '0x%x'\n", resolution->pixelResolution);
					hal_dbg("Leena aspectRatio: '0x%x'\n", resolution->aspectRatio);
					hal_dbg("Leena frameRate: '%s'\n", getdsVideoFrameRateString(resolution->frameRate));
					hal_dbg("Leena interlaced: '%d'\n", resolution->interlaced);
					return dsERR_NONE;
				} else {
					hal_err("Failed to convert westeros resolution '%s' to dsVideoPortResolution_t\n", wstresolution);
					return dsERR_GENERAL;
				}
			} else {
				hal_err("Failed to parse westerosRWWrapper response; ']' not found.\n");
				return dsERR_GENERAL;
			}
		} else {
			hal_err("Failed to parse westerosRWWrapper response; 'mode ' not found.\n");
			return dsERR_GENERAL;
		}
	}
	return dsERR_GENERAL;
}

/**
 * @brief Sets the display resolution of specified video port.
 *
 * This function sets the resolution of the specified video port.
 *
 * @param[in] handle        - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[in] resolution    - Video resolution. Please refer
 * ::dsVideoPortResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetResolution()
 */
dsError_t dsSetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
{
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == resolution) {
        hal_err("handle(%p) is invalid or resolution(%p) is null.\n", handle, resolution);
        return dsERR_INVALID_PARAM;
    }
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        hal_dbg("Setting HDMI resolution name'%s'\n", resolution->name);
		hal_dbg("Setting HDMI resolution pixelResolution '0x%x'\n", resolution->pixelResolution);
		hal_dbg("Setting HDMI resolution aspectRatio '0x%x'\n", resolution->aspectRatio);
		hal_dbg("Setting HDMI resolution frameRate '%s'\n", getdsVideoFrameRateString(resolution->frameRate));
		hal_dbg("Setting HDMI resolution interlaced '%d'\n", resolution->interlaced);
		if (!dsVideoPortFrameRate_isValid(resolution->frameRate) ||
			!dsVideoPortPixelResolution_isValid(resolution->pixelResolution)) {
			hal_err("Invalid param; framerate 0x%x or pixelResolution 0x%x\n", resolution->frameRate, resolution->pixelResolution);
			return dsERR_INVALID_PARAM;
		}
		char cmd[256] = {0};
		char data[128] = {0};
		char westerosRes[64] = {0};
		if (convertkResolutionToWesterosResolution(resolution, westerosRes, sizeof(westerosRes)) == false) {
			hal_err("Failed to convert resolution '%s' to westeros format\n", resolution->name);
			return dsERR_GENERAL;
		}
		snprintf(cmd, sizeof(cmd), "export XDG_RUNTIME_DIR=/run; westeros-gl-console set mode %s", westerosRes);
		hal_info("Westeros Resolution CMD: '%s'\n", cmd);
		if (westerosRWWrapper(cmd, data, sizeof(data))) {
			hal_info("westerosRWWrapper response:'%s'\n", data);
			// Response: [0: mode 1280x720px60]
			// extract string between 'mode ' and ']' and compare against westerosRes
			char *start = strstr(data, "mode ");
			if (start) {
				start += strlen("mode ");
				char *end = strstr(start, "]");
				if (end) {
					*end = '\0';
					if (strcmp(start, westerosRes) == 0) {
						hal_info("Resolution set successfully\n");
					} else {
						hal_err("Failed to set resolution\n");
						return dsERR_GENERAL;
					}
				} else {
					hal_err("Failed to parse response\n");
					return dsERR_GENERAL;
				}
			} else {
				hal_err("Failed to parse response\n");
				return dsERR_GENERAL;
			}
		}
    } else {
	    hal_err("Video port type not supported\n");
	    return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Terminates the underlying Video Port sub-system.
 *
 * This function must terminate all the video output ports and any associated
 * resources.
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
dsError_t  dsVideoPortTerm()
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    vchi_tv_uninit();
    _halhdcpcallback = NULL;
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
static bool isValidVopHandle(intptr_t m_handle) {
    for (int i = 0; i < dsVIDEOPORT_TYPE_MAX; i++) {
        //hal_info("Checking if m_handle(%p) is a match with &_vopHandles[%d][0](%p).\n", m_handle, i, &_vopHandles[i][0]);
        if ((intptr_t)&_vopHandles[i][0] == m_handle) {
            hal_info("m_handle(%p) is a match.\n", m_handle);
            return true;
        }
    }
    return false;
}

/**
 * @brief Checks whether a video port is active or not.
 *
 * This function is used to indicate whether a video port is active or not. A
 * HDMI output port is active if it is connected to the active port of sink
 * device.
 *
 * @param[in]  handle   - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] active   - Connection state of the video port
 *                          ( @a true if connected, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @see dsSetActiveSource()
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsVideoPortActive(intptr_t handle, bool *active)
{
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    TV_DISPLAY_STATE_T tvstate;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == active) {
        hal_err("handle(%p) is invalid or active(%p) is null.\n", handle, active);
        return dsERR_INVALID_PARAM;
    }
    /* Default to false */
    *active = false;

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        if (vc_tv_get_display_state( &tvstate ) == 0) {
            hal_dbg("vc_tv_get_display_state: 0x%x\n", tvstate.state);
            if (tvstate.state & VC_HDMI_HDMI)
                *active = true;
            else if (tvstate.state & VC_HDMI_UNPLUGGED)
                *active = false;
            else
                hal_warn("Cannot find HDMI state\n");
        }
    } else {
        hal_err("Video port type not supported\n");
        return dsERR_INVALID_PARAM;
    }
    return dsERR_NONE;
}

/**
 * @brief Gets the HDCP protocol version of the device.
 *
 * @param[in] handle                - Handle of the video port returned from
 * dsGetVideoPort()
 * @param [out] protocolVersion     - HDCP protocol version.  Please refer
 * ::dsHdcpProtocolVersion_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsGetHDCPProtocol(intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    // Ref: https://forums.raspberrypi.com/viewtopic.php?t=278193
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        hal_err("handle(%p) is invalid or protocolVersion(%p) is NULL.\n", handle, protocolVersion);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDCPReceiverProtocol (intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    // Ref: https://forums.raspberrypi.com/viewtopic.php?t=278193
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        hal_err("handle(%p) is invalid or protocolVersion(%p) is null.\n", handle, protocolVersion);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDCPCurrentProtocol(intptr_t handle, dsHdcpProtocolVersion_t *protocolVersion)
{
    // Ref: https://forums.raspberrypi.com/viewtopic.php?t=278193
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == protocolVersion)
    {
        hal_err("handle(%p) is invalid or protocolVersion(%p) is null.\n", handle, protocolVersion);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetTVHDRCapabilities(intptr_t handle, int *capabilities)
{
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == capabilities)
    {
        hal_err("handle(%p) is invalid or capabilities(%p) is null.\n", handle, capabilities);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported resolutions of TV.
 *
 * This function is used to get TV supported resolutions of TV/display device.
 *
 * @param[in] handle            - Handle of the video port(TV) returned from
 * dsGetVideoPort()
 * @param [out] resolutions     - OR-ed value supported resolutions.  Please
 * refer ::dsTVResolution_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsSupportedTvResolutions(intptr_t handle, int *resolutions)
{
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || (NULL == resolutions)) {
        hal_err("resolutions(%p) or handle(%p) is invalid.\n", resolutions, handle);
        return dsERR_INVALID_PARAM;
    }

    hal_info("handle = %p\n", vopHandle);
    hal_info("*handle = %p\n", *vopHandle);
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_MODE_ID];
        HDMI_RES_GROUP_T group;
        uint32_t mode;
        int num_of_modes;
        int i;
        const dsTVResolution_t *TVVideoResolution = NULL;
        num_of_modes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, modeSupported,
                vcos_countof(modeSupported),
                &group,
                &mode);
        if (num_of_modes < 0) {
            hal_err("Failed to get modes vc_tv_hdmi_get_supported_modes_new\n");
            return dsERR_GENERAL;
        }
        hal_info("num_of_modes = %d\n", num_of_modes);
        for (i = 0; i < num_of_modes; i++) {
            hal_info("[%d] mode %u: %ux%u%s%uHz\n", i, modeSupported[i].code, modeSupported[i].width,
                    modeSupported[i].height, (modeSupported[i].scan_mode?"i":"p"), modeSupported[i].frame_rate);
            // modeSupported[i].code is VIC
            TVVideoResolution = getResolutionFromVic(modeSupported[i].code);
            if (TVVideoResolution != NULL) {
                hal_info("VIC %u TVVideoResolution = 0x%x\n", getVicFromResolution(*TVVideoResolution), *TVVideoResolution);
                *resolutions |= *TVVideoResolution;
            }
        }
    } else {
        hal_err("Get supported resolution for TV on Non HDMI Port\n");
        return dsERR_INVALID_PARAM;
    }
    hal_dbg("Supported resolutions: 0x%x\n", *resolutions);
    return dsERR_NONE;
}

/**
 * @brief Checks if the connected display supports the audio surround.
 *
 * This function is used to check if the display connected to video port
 * supports the audio surround.
 *
 * @param[in]  handle   - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] surround - Audio surround support  ( @a true if display supports
 * surround sound or @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t  dsIsDisplaySurround(intptr_t handle, bool *surround)
{
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if(!isValidVopHandle(handle) ||surround == NULL)
    {
        hal_err("handle(%p) is invalid or surround(%p) is null.\n", handle, surround);
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
 * @param[in]  handle   - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] surround - Surround mode .Please refer :: dsSURROUNDMode_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t  dsGetSurroundMode(intptr_t handle, int *surround)
{
    hal_info("invoked.\n");
    if(false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if(!isValidVopHandle(handle)|| surround == NULL)
    {
        hal_err("handle(%p) is invalid or surround(%p) is null.\n", handle, surround);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsVideoFormatUpdateRegisterCB(dsVideoFormatUpdateCB_t cb)
{
    hal_info("invoked.\n");
    if (cb == NULL) {
        hal_err("Invalid param, cb(%p).\n", cb);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the specified video port as active source.
 *
 * @param[in] handle    - Handle of the video port returned from
 * dsGetVideoPort()
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @see dsIsVideoPortActive()
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsSetActiveSource(intptr_t handle)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle)) {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the current HDCP status of the specified video port.
 *
 * @param[in] handle    - Handle of the video port returned from
 * dsGetVideoPort()
 * @param[out] status   - HDCP status of the video port.  Please refer
 * ::dsHdcpStatus_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() and dsGetVideoPort() must be called before calling
 * this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableHDCP()
 */
dsError_t dsGetHDCPStatus(intptr_t handle, dsHdcpStatus_t *status)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (status == NULL || !isValidVopHandle(handle)) {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetForceDisable4KSupport(intptr_t handle, bool disable)
{
    hal_info("invoked with disable %d.\n", disable);
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid.\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetForceDisable4KSupport(intptr_t handle, bool *disable)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || disable == NULL) {
        hal_err("handle(%p) is invalid or disable(%p) is null.\n", handle, disable);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetVideoEOTF(intptr_t handle, dsHDRStandard_t *video_eotf)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (video_eotf == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or video_eotf(%p) is null.\n", handle, video_eotf);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetMatrixCoefficients(intptr_t handle, dsDisplayMatrixCoefficients_t *matrix_coefficients)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (matrix_coefficients == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or matrix_coefficients(%p) is null.\n", handle, matrix_coefficients);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetColorDepth(intptr_t handle, unsigned int *color_depth)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (color_depth == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or color_depth(%p) is null.\n", handle, color_depth);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetColorSpace(intptr_t handle, dsDisplayColorSpace_t *color_space)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (color_space == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or color_space(%p) is null.\n", handle, color_space);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetQuantizationRange(intptr_t handle, dsDisplayQuantizationRange_t *quantization_range)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (quantization_range == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or quantization_range(%p) is null.\n", handle, quantization_range);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetCurrentOutputSettings(intptr_t handle, dsHDRStandard_t *video_eotf, dsDisplayMatrixCoefficients_t *matrix_coefficients, dsDisplayColorSpace_t *color_space, unsigned int *color_depth, dsDisplayQuantizationRange_t *quantization_range)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (video_eotf == NULL || matrix_coefficients == NULL ||
            color_space == NULL || color_depth == NULL ||
            quantization_range == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or one of the params is NULL.\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsIsOutputHDR(intptr_t handle, bool *hdr)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdr == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or hdr(%p) is null.\n", handle, hdr);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsResetOutputToSDR()
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetHdmiPreference(intptr_t handle, dsHdcpProtocolVersion_t *hdcpCurrentProtocol)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdcpCurrentProtocol == NULL ||!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or hdcpCurrentProtocol(%p) is null.\n", handle, hdcpCurrentProtocol);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHdmiPreference(intptr_t handle, dsHdcpProtocolVersion_t *hdcpCurrentProtocol)
{
    hal_info("invoked.\n");
    if	(false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (hdcpCurrentProtocol == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or hdcpCurrentProtocol(%p) is null.\n", handle, hdcpCurrentProtocol);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetIgnoreEDIDStatus(intptr_t handle, bool *status)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || status == NULL) {
        hal_err("handle(%p) is invalid or status(%p) is null.\n", handle, status);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetBackgroundColor(intptr_t handle, dsVideoBackgroundColor_t color)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || color != dsVIDEO_BGCOLOR_BLUE
            || color != dsVIDEO_BGCOLOR_BLACK || color != dsVIDEO_BGCOLOR_NONE) {
        hal_err("handle(%p) is invalid or color(%d) is invalid.\n", handle, color);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetForceHDRMode(intptr_t handle, dsHDRStandard_t mode)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid.\n", handle);
        return dsERR_INVALID_PARAM;
    }
    if (mode != dsHDRSTANDARD_NONE && mode != dsHDRSTANDARD_HDR10 && mode != dsHDRSTANDARD_HLG &&
            mode != dsHDRSTANDARD_DolbyVision && mode != dsHDRSTANDARD_TechnicolorPrime &&
            mode != dsHDRSTANDARD_HDR10PLUS && mode != dsHDRSTANDARD_SDR) {
        hal_err("mode(%d) is invalid.\n", mode);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsColorDepthCapabilities(intptr_t handle, unsigned int *colorDepthCapability )
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepthCapability == NULL ||!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or colorDepthCapability(%p) is null.\n", handle, colorDepthCapability);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t *colorDepth)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepth == NULL ||!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or colorDepth(%p) is null.\n", handle, colorDepth);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t colorDepth)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle)) {
        return dsERR_INVALID_PARAM;
    }
    if (colorDepth != dsDISPLAY_COLORDEPTH_UNKNOWN && colorDepth != dsDISPLAY_COLORDEPTH_8BIT &&
            colorDepth != dsDISPLAY_COLORDEPTH_10BIT && colorDepth != dsDISPLAY_COLORDEPTH_12BIT &&
            colorDepth != dsDISPLAY_COLORDEPTH_AUTO) {
        hal_err("colorDepth(%d) is invalid.\n", colorDepth);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
