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
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>

#include "dsUtl.h"
#include "dsError.h"
#include "dsTypes.h"
#include "dsVideoPort.h"
#include "dsVideoResolutionSettings.h"
#include "dsDisplay.h"
#include "dsAudio.h"
#include "dshalEdidParser.h"
#include "dshalUtils.h"
#include "dshalLogger.h"
#include "dsVideoPortSettings.h"
#include "dsVideoDevice.h"

/* Forward declarations */
dsError_t dsGetAudioEncoding(intptr_t handle, dsAudioEncoding_t *encoding);
extern dsRegisterFrameratePreChangeCB_t dsVideoDeviceGetFrameratePreChangeCB(void);
extern dsRegisterFrameratePostChangeCB_t dsVideoDeviceGetFrameratePostChangeCB(void);

static bool _bIsVideoPortInitialized = false;
static bool isValidVopHandle(intptr_t handle);
static const char *dsVideoGetResolution(void);
#define MAX_HDMI_MODE_ID (127)

#ifndef XDG_RUNTIME_DIR
#define XDG_RUNTIME_DIR     "/tmp"
#endif

dsHDCPStatusCallback_t _halhdcpcallback = NULL;

typedef struct _VOPHandle_t {
    dsVideoPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
    bool m_isEnabled;
} VOPHandle_t;

static VOPHandle_t _vopHandles[dsVIDEOPORT_TYPE_MAX][2] = {};

static dsVideoPortResolution_t _resolution;
static bool _bIgnoreEDID = false;

static bool drm_get_hdmi_connector_state(bool *connected, bool *enabled)
{
    return dsGetHdmiConnectorState(connected, enabled);
}

static dsError_t getHdmiEdidForConnectedDisplay(dsVideoPortType_t video_port_type,
        int video_port_index,
        unsigned char **edid_buf,
        int *edid_len)
{
    intptr_t dispHandle = 0;

    if (edid_buf == NULL || edid_len == NULL) {
        return dsERR_INVALID_PARAM;
    }

    *edid_buf = (unsigned char *)calloc(MAX_EDID_BYTES_LEN, sizeof(unsigned char));
    if (*edid_buf == NULL) {
        return dsERR_GENERAL;
    }

    if (dsGetDisplay(video_port_type, video_port_index, &dispHandle) != dsERR_NONE ||
            dsGetEDIDBytes(dispHandle, *edid_buf, edid_len) != dsERR_NONE ||
            *edid_len < DSHAL_EDID_BLOCK_SIZE) {
        free(*edid_buf);
        *edid_buf = NULL;
        *edid_len = 0;
        return dsERR_GENERAL;
    }

    return dsERR_NONE;
}

typedef struct {
    int *capabilities;
} HdrParseContext_t;

static bool parseHdrFromCtaDataBlock(int tag, const unsigned char *data, int dataLen, void *context)
{
    HdrParseContext_t *ctx = (HdrParseContext_t *)context;

    if (ctx == NULL || ctx->capabilities == NULL || data == NULL) {
        return false;
    }

    if (tag == DSHAL_EDID_CTA_EXTENDED_TAG && dataLen >= 2 &&
            data[0] == DSHAL_EDID_EXT_TAG_HDR_STATIC_METADATA) {
        unsigned char eotf_flags = data[1];
        if (eotf_flags & 0x01) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_SDR;
        }
        if (eotf_flags & 0x02) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_TechnicolorPrime;
        }
        if (eotf_flags & DSHAL_EDID_EOTF_HDR10_BIT) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_HDR10;
        }
        if (eotf_flags & DSHAL_EDID_EOTF_HLG_BIT) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_HLG;
        }
    } else if (tag == DSHAL_EDID_CTA_VENDOR_SPECIFIC_TAG && dataLen >= 3) {
        if (data[0] == DSHAL_EDID_DOLBY_VSIF_OUI_BYTE0 &&
                data[1] == DSHAL_EDID_DOLBY_VSIF_OUI_BYTE1 &&
                data[2] == DSHAL_EDID_DOLBY_VSIF_OUI_BYTE2) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_DolbyVision;
        }

        if (data[0] == DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE0 &&
                data[1] == DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE1 &&
                data[2] == DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE2) {
            *(ctx->capabilities) |= (int)dsHDRSTANDARD_HDR10PLUS;
        }
    }

    return false;
}

typedef struct {
    int *resolutions;
} TvResolutionParseContext_t;

static bool parseSupportedResolutionsFromCtaDataBlock(int tag, const unsigned char *data, int dataLen, void *context)
{
    TvResolutionParseContext_t *ctx = (TvResolutionParseContext_t *)context;

    if (ctx == NULL || ctx->resolutions == NULL || data == NULL) {
        return false;
    }

    if (tag != DSHAL_EDID_CTA_DATA_BLOCK_TAG_VIDEO || dataLen <= 0) {
        return false;
    }

    for (int i = 0; i < dataLen; i++) {
        int vic = data[i] & 0x7F;
        const dsTVResolution_t *tvRes = getResolutionFromVic(vic);
        if (tvRes != NULL) {
            *(ctx->resolutions) |= (int)(*tvRes);
        }
    }

    return false;
}

typedef struct {
    bool *surround;
} SurroundParseContext_t;

static bool parseSurroundFromCtaDataBlock(int tag, const unsigned char *data, int dataLen, void *context)
{
    SurroundParseContext_t *ctx = (SurroundParseContext_t *)context;

    if (ctx == NULL || ctx->surround == NULL || data == NULL) {
        return false;
    }

    if (tag != DSHAL_EDID_CTA_DATA_BLOCK_TAG_AUDIO || dataLen < DSHAL_EDID_CTA_SHORT_AUDIO_DESCRIPTOR_LEN) {
        return false;
    }

    for (int i = 0; (i + (DSHAL_EDID_CTA_SHORT_AUDIO_DESCRIPTOR_LEN - 1)) < dataLen; i += DSHAL_EDID_CTA_SHORT_AUDIO_DESCRIPTOR_LEN) {
        int channels = (data[i] & 0x07) + 1;
        if (channels > 2) {
            *(ctx->surround) = true;
            return true;
        }
    }

    return false;
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
    if (!isValidVopHandle(handle) || NULL == cb) {
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
    _resolution = kResolutionsSettings[kDefaultResIndex];

    /* HDCP callback registration removed: tvservice eliminated, HDCP status assumed authenticated by default */
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
    /* Raspberry Pi 4 backend exposes HDMI only in this HAL implementation. */
    if (type != dsVIDEOPORT_TYPE_HDMI) {
        hal_err("unsupported port type %d\n", type);
        return dsERR_OPERATION_NOT_SUPPORTED;
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
    bool drmConnected = false;
    bool drmEnabled = false;

    if (false == _bIsVideoPortInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == enabled)
    {
        hal_err("handle(%p) is invalid or enabled(%p) is null.\n", handle, enabled);
        return dsERR_INVALID_PARAM;
    }

    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI)
    {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (drm_get_hdmi_connector_state(&drmConnected, &drmEnabled)) {
        (void)drmConnected;
        vopHandle->m_isEnabled = drmEnabled;
    }

    *enabled = vopHandle->m_isEnabled;
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

    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        int respEnbaled = 0, respStatus = 0;
        char cmd[256] = {0};
        char resp[256] = {0};
        int cmdLen = snprintf(cmd, sizeof(cmd), "set display enable %d", enabled);
        if (cmdLen < 0 || cmdLen >= (int)sizeof(cmd)) {
            hal_err("Command buffer is too small\n");
            return dsERR_GENERAL;
        }

        if (!westerosGLConsoleRWWrapper(cmd, resp, sizeof(resp))) {
            hal_err("Failed to run '%s', got response '%s'\n", cmd, resp);
            return dsERR_GENERAL;
        }
    } else {
        hal_err("Unsupported video port type: %d\n", vopHandle->m_vType);
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
    bool drmConnected = false;
    bool drmEnabled = false;

    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == connected) {
        hal_err("handle(%p) is invalid or connected(%p) is null.\n", handle, connected);
        return dsERR_INVALID_PARAM;
    }

    *connected = false;

    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled)) {
        hal_err("Unable to read DRM HDMI connector state\n");
        return dsERR_GENERAL;
    }

    vopHandle->m_isEnabled = drmEnabled;
    *connected = drmConnected;

    hal_dbg("DRM HDMI state: connected=%d enabled=%d\n", drmConnected, drmEnabled);
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
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == resolution) {
        hal_err("handle(%p) is invalid or resolution(%p) is NULL.\n", handle, resolution);
        return dsERR_INVALID_PARAM;
    }
    /* Query the active mode from westeros-gl-console/DRM. */
    const char *resolution_name = dsVideoGetResolution();
    if (resolution_name) {
        strncpy(resolution->name, resolution_name, sizeof(resolution->name) - 1);
        resolution->name[sizeof(resolution->name) - 1] = '\0';
        return dsERR_NONE;
    }
    resolution->name[0] = '\0';
    hal_err("Failed to resolve current display mode to RDK resolution token\n");
    return dsERR_GENERAL;
}

/**
 * @brief Gets the current video resolution.
 *
 * This function queries the current video resolution from the underlying
 * platform and normalizes it to a standard format.
 *
 * @return const char* - Normalized resolution string or NULL if failed.
 */
static const char* dsVideoGetResolution(void)
{
    hal_info("invoked.\n");
    char resName[32] = {'\0'};
    char normalizedRes[32] = {'\0'};
    const char *resolution_name = NULL;
    char respBuf[256] = {'\0'};
    if (westerosGLConsoleRWWrapper("get mode", respBuf, sizeof(respBuf))) {
        strncpy(resName, respBuf, sizeof(resName) - 1);
        resName[sizeof(resName) - 1] = '\0';

        int modeStatus = -1;
        char modeToken[32] = {'\0'};
        if (sscanf(resName, "%d: mode %31s", &modeStatus, modeToken) == 2 && modeStatus == 0) {
            strncpy(resName, modeToken, sizeof(resName) - 1);
            resName[sizeof(resName) - 1] = '\0';
        }
    } else {
        hal_err("Failed to get current mode, got response '%s'\n", respBuf);
        return NULL;
    }

    size_t resLen = strlen(resName);
    while (resLen > 0 && isspace((unsigned char)resName[resLen - 1])) {
        resName[--resLen] = '\0';
    }

    int width = -1;
    int height = -1;
    int rate = -1;
    char scanMode = '\0';
    if (sscanf(resName, "%dx%d%c%d", &width, &height, &scanMode, &rate) == 4) {
        scanMode = (char)tolower((unsigned char)scanMode);
        if (width > 0 && height > 0 && rate > 0 && (scanMode == 'p' || scanMode == 'i')) {
            snprintf(normalizedRes, sizeof(normalizedRes), "%d%c%d", height, scanMode, rate);
        }
    }

    if (normalizedRes[0] == '\0') {
        strncpy(normalizedRes, resName, sizeof(normalizedRes) - 1);
        normalizedRes[sizeof(normalizedRes) - 1] = '\0';
    }

    hal_info("resName '%s', normalized '%s'\n", resName, normalizedRes);

    for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
        const char *mapRes = resolutionMap[i].rdkRes;

        size_t len = strlen(mapRes);
        int hasRate = (len > 0 && isdigit((unsigned char)mapRes[len-1]));

        if (hasRate) {
            if (strcmp(mapRes, normalizedRes) == 0) {
                resolution_name = mapRes;
                break;
            }
        } else {
            char temp[32];
            snprintf(temp,sizeof(temp), "%s60", mapRes);

            if (strcmp(temp, normalizedRes) == 0 || strcmp(mapRes, normalizedRes) == 0) {
                resolution_name = mapRes;
                break;
            }
        }
    }
    if (resolution_name != NULL) {
        hal_info("resolution_name %s\n", resolution_name);
    } else {
        hal_err("Failed to find matching resolution for mode '%s' (normalized '%s')\n", resName, normalizedRes);
    }

    return resolution_name;
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
    /* Auto Select uses 720p. Should be converted to dsVideoPortResolution_t = 720p in DS-VOPConfig, not here */
    hal_info("invoked.\n");
    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;

    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle) || NULL == resolution || resolution->name[0] == '\0' ||
        !dsVideoPortPixelResolution_isValid(resolution->pixelResolution) ||
        !dsVideoPortAspectRatio_isValid(resolution->aspectRatio) ||
        !dsVideoPortStereoScopicMode_isValid(resolution->stereoScopicMode) ||
        !dsVideoPortFrameRate_isValid(resolution->frameRate) ||
        !dsVideoPortScanMode_isValid((int)resolution->interlaced)) {
        hal_err("dsSetResolution dsERR_INVALID_PARAM - Invalid handle or resolution parameters\n");
        return dsERR_INVALID_PARAM;
    }
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        hal_dbg("Setting HDMI resolution '%s'\n", resolution->name);
        char cmdBuf[256] = {'\0'};
        char respBuf[256] = {'\0'};
        int width = -1, height = -1;
        int rate = 60;
        char interlaced = 'n';
        if (sscanf (resolution->name, "%dx%dp%d", &width, &height, &rate) == 3) {
            interlaced = 'p';
        }
        else if (sscanf(resolution->name, "%dx%di%d", &width, &height, &rate ) == 3) {
            interlaced = 'i';
        }
        else if (sscanf(resolution->name, "%dx%dx%d", &width, &height, &rate ) == 3) {
            interlaced = 'p';
        }
        else if (sscanf(resolution->name, "%dx%d", &width, &height ) == 2) {
            interlaced = 'p';
        }
        else if (sscanf(resolution->name, "%dp%d", &height, &rate ) == 2) {
            interlaced = 'p';
            width= -1;
        }
        else if (sscanf(resolution->name, "%di%d", &height, &rate ) == 2) {
            interlaced = 'i';
            width= -1;
        }
        else if (sscanf(resolution->name, "%d%c", &height, &interlaced ) == 2) {
            width= -1;
            rate = 60;
        }

        interlaced = (char)tolower((unsigned char)interlaced);

        //if width is missing, set it manually
        if (height > 0) {
            if (width < 0) {
                switch (height)
                {
                    case 480:
                    case 576:
                        width= 720;
                        break;
                    case 720:
                        width= 1280;
                        break;
                    case 1080:
                        width= 1920;
                        break;
                    case 1440:
                        width= 2560;
                        break;
                    case 2160:
                        width= 3840;
                        break;
                    case 2880:
                        width= 5120;
                        break;
                    case 4320:
                        width= 7680;
                        break;
                    default:
                        break;
                }
            }
        }

        if (width <= 0 || height <= 0 || rate <= 0 || (interlaced != 'p' && interlaced != 'i')) {
            hal_err("Unsupported resolution format '%s' parsed as %dx%d%c%d\n",
                    resolution->name, width, height, interlaced, rate);
            return dsERR_INVALID_PARAM;
        }

        //extended command to make resolution setting more synchronous
        int snprintfResult = snprintf(cmdBuf, sizeof(cmdBuf), "set mode %dx%d%c%d", width, height, interlaced, rate);
        if (snprintfResult < 0 || snprintfResult >= (int)sizeof(cmdBuf)) {
            hal_err("Command buffer too small or snprintf error\n");
            return dsERR_GENERAL;
        }
        dsRegisterFrameratePreChangeCB_t frameratePreCB = dsVideoDeviceGetFrameratePreChangeCB();
        if (frameratePreCB) {
            frameratePreCB((unsigned int)rate);
        }
        if (!westerosGLConsoleRWWrapper(cmdBuf, respBuf, sizeof(respBuf))) {
            hal_err("Failed to run '%s', got response '%s'\n", cmdBuf, respBuf);
            return dsERR_GENERAL;
        }
        int cmdStatus = -1;
        bool isStatusPrefixedSuccess = (sscanf(respBuf, "%d:", &cmdStatus) == 1 && cmdStatus == 0);
        if (strcmp(respBuf, "OK") != 0 && !isStatusPrefixedSuccess) {
            hal_err("Failed to set resolution with command '%s', got response '%s'\n", cmdBuf, respBuf);
            return dsERR_GENERAL;
        }
        /* Verify the mode actually took effect by re-querying the active mode. */
        const char *activeRes = dsVideoGetResolution();
        if (activeRes == NULL || strcmp(activeRes, resolution->name) != 0) {
            hal_err("Resolution mismatch after set: requested '%s', active '%s'\n",
                    resolution->name, activeRes ? activeRes : "<unknown>");
            return dsERR_GENERAL;
        }
        dsRegisterFrameratePostChangeCB_t frameratePostCB = dsVideoDeviceGetFrameratePostChangeCB();
        if (frameratePostCB) {
            frameratePostCB((unsigned int)rate);
        }
    } else {
        hal_err("Unsupported video port type: %d\n", vopHandle->m_vType);
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
    /* HDCP callback unregistration removed: tvservice eliminated */
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
    bool connected = false;
    bool enabled = false;
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
        if (!drm_get_hdmi_connector_state(&connected, &enabled)) {
            hal_err("Failed to get HDMI connector state\n");
            return dsERR_GENERAL;
        }
        *active = (connected && enabled);
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

/**
 * @brief Gets the HDR capabilities of the TV/display device
 *
 * This function is used to get the HDR capabilities of the TV/display device.
 *
 * @param[in] handle            - Handle of the video port(TV) returned from dsGetVideoPort()
 * @param [out] capabilities    - Bitwise OR-ed value of supported HDR standards.  Please refer ::dsHDRStandard_t
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
 * @see dsIsOutputHDR()
 *
 * @warning  This API is Not thread safe.
 */
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    bool isConnected = false;
    dsError_t connRet = dsIsDisplayConnected(handle, &isConnected);
    if (connRet != dsERR_NONE || !isConnected) {
        // Return an error if display is not connected, we cannot determine connected display's HDR capabilities.
        return dsERR_GENERAL;
    }

    *capabilities = (int)dsHDRSTANDARD_SDR;

    unsigned char *edid_buf = NULL;
    int edid_len = 0;

    if (getHdmiEdidForConnectedDisplay(dsVIDEOPORT_TYPE_HDMI, 0, &edid_buf, &edid_len) != dsERR_NONE) {
        hal_warn("EDID unavailable; defaulting HDR capabilities to SDR\n");
        return dsERR_NONE;
    }

    HdrParseContext_t ctx = { .capabilities = capabilities };
    (void)dshalEdidForEachCtaDataBlock(edid_buf, edid_len, parseHdrFromCtaDataBlock, &ctx);

    free(edid_buf);
    hal_dbg("TV HDR capabilities from EDID: 0x%x\n", *capabilities);
    return dsERR_NONE;
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

    hal_info("handle = %p\n", (void *)vopHandle);
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        *resolutions = 0;

        bool isConnected = false;
        dsError_t connRet = dsIsDisplayConnected(handle, &isConnected);
        if (connRet != dsERR_NONE || !isConnected) {
            // Return an error if display is not connected, we cannot determine supported resolutions.
            hal_err("Display not connected; cannot determine supported resolutions\n");
            return dsERR_GENERAL;
        }

        /* Enumerate only the VICs advertised in the connected display's EDID
         * to avoid reporting unsupported modes from the static resolution map. */
        unsigned char *edid_buf = NULL;
        int edid_len = 0;

        if (getHdmiEdidForConnectedDisplay(dsVIDEOPORT_TYPE_HDMI, 0, &edid_buf, &edid_len) != dsERR_NONE) {
            hal_warn("EDID unavailable; cannot report supported TV resolutions\n");
            return dsERR_NONE;
        }

        /* Walk CTA-861 extension blocks for Short Video Descriptors (SVDs). */
        TvResolutionParseContext_t ctx = { .resolutions = resolutions };
        (void)dshalEdidForEachCtaDataBlock(edid_buf, edid_len, parseSupportedResolutionsFromCtaDataBlock, &ctx);

        free(edid_buf);
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    bool isConnected = false;
    dsError_t connRet = dsIsDisplayConnected(handle, &isConnected);
    if (connRet != dsERR_NONE || !isConnected) {
        hal_err("Display not connected; cannot determine surround support\n");
        return dsERR_GENERAL;
    }

    *surround = false;
    unsigned char *edid_buf = NULL;
    int edid_len = 0;

    if (getHdmiEdidForConnectedDisplay(dsVIDEOPORT_TYPE_HDMI, 0, &edid_buf, &edid_len) != dsERR_NONE) {
        hal_warn("EDID unavailable; cannot determine surround support\n");
        return dsERR_GENERAL;
    }

    SurroundParseContext_t ctx = { .surround = surround };
    (void)dshalEdidForEachCtaDataBlock(edid_buf, edid_len, parseSurroundFromCtaDataBlock, &ctx);

    free(edid_buf);
    hal_info("Display surround support: %s\n", *surround ? "true" : "false");
    return dsERR_NONE;
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
dsError_t dsGetSurroundMode(intptr_t handle, int *surround)
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

    bool isConnected = false;
    dsError_t connRet = dsIsDisplayConnected(handle, &isConnected);
    if (connRet != dsERR_NONE || !isConnected) {
        hal_err("Display not connected; cannot determine surround mode\n");
        return dsERR_GENERAL;
    }

    intptr_t audioHandle = (intptr_t)NULL;
    dsAudioEncoding_t encoding = dsAUDIO_ENC_PCM;

    if (dsGetAudioPort(dsAUDIOPORT_TYPE_HDMI, 0, &audioHandle) == dsERR_NONE &&
            dsGetAudioEncoding(audioHandle, &encoding) == dsERR_NONE) {
        if (encoding == dsAUDIO_ENC_EAC3) {
            *surround = dsSURROUNDMODE_DDPLUS;
        } else if (encoding == dsAUDIO_ENC_AC3) {
            *surround = dsSURROUNDMODE_DD;
        } else {
            *surround = dsSURROUNDMODE_NONE;
        }
        return dsERR_NONE;
    }

    /* If current audio state is unavailable, return a safe default. */
    *surround = dsSURROUNDMODE_NONE;
    hal_warn("Unable to read current audio encoding; defaulting surround mode to NONE\n");
    return dsERR_NONE;
}

dsError_t dsVideoFormatUpdateRegisterCB(dsVideoFormatUpdateCB_t cb)
{
    hal_info("invoked.\n");

    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

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

/**
 * @brief Gets the current video Electro-Optical Transfer Function (EOT) value.
 *
 * This function is used to get the current HDR format on a specified video port.
 *
 * @param[in]  handle       - Handle of the video port returned from dsGetVideoPort()
 * @param[out] video_eotf   - EOTF value.  Please refer ::dsHDRStandard_t
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
    *video_eotf = (dsHDRStandard_t)dsHDRSTANDARD_SDR;

    return dsERR_NONE;
}

/**
 * @brief Gets the current matrix coefficients value.
 *
 * This function is used to get the current matrix coefficient value of the specified video port.
 * For source devices, this function would return dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN  when TV is not connected.
 *
 * @param[in]  handle               - Handle of the video port returned from dsGetVideoPort()
 * @param[out] matrix_coefficients  - pointer to matrix coefficients value.  Please refer ::dsDisplayMatrixCoefficients_t
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
dsError_t dsGetMatrixCoefficients(intptr_t handle, dsDisplayMatrixCoefficients_t *matrix_coefficients)
{
    hal_info("invoked.\n");
    bool drmConnected = false;
    bool drmEnabled = false;

    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (matrix_coefficients == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or matrix_coefficients(%p) is null.\n", handle, matrix_coefficients);
        return dsERR_INVALID_PARAM;
    }

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled)) {
        hal_err("Failed to get HDMI connector state\n");
        return dsERR_GENERAL;
    }

    if (!drmConnected) {
        *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
        hal_warn("HDMI not connected (DRM), matrix coefficients UNKNOWN\n");
        return dsERR_NONE;
    }

    *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_BT_709;
    hal_dbg("Matrix coefficient defaulted to BT_709: %u\n", *matrix_coefficients);
    return dsERR_NONE;
}

/**
 * @brief Gets the color depth value of specified video port.
 *
 * For sink devices, this function returns the default color depth, which is platform dependent.
 *
 * For source devices, this function is used to get the current color depth value of specified video port.
 * Typically for UHD resolution, the color depth is 10/12-bit, while for non-UHD resolutions, it is 8-bit
 *
 * @param[in]  handle       - Handle of the video port returned from dsGetVideoPort()
 * @param[out] color_depth  - pointer to color depth values.Please refer :: dsDisplayColorDepth_t
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* HDMI will be attached when this gets invoked - default to 8-bit color depth */
    *color_depth = dsDISPLAY_COLORDEPTH_8BIT;
    hal_dbg("Color depth defaulted to 8-bit (RPi4 hardware limitation)\n");
    return dsERR_NONE;
}

/**
 * @brief Gets the color space setting of specified video port.
 *
 * For sink devices, this function returns the default color space setting, which is platform dependent.
 *
 * For source devices, this function is used to get the current color space setting of specified video port.
 * The color space is typically YCbCr.
 *
 * @param[in]  handle       - Handle of the video port returned from dsGetVideoPort()
 * @param[out] color_space  - pointer to color space value. Please refer ::dsDisplayColorSpace_t
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    *color_space = dsDISPLAY_COLORSPACE_RGB;
    hal_dbg("Color space defaulted to RGB\n");
    return dsERR_NONE;
}

/**
 * @brief Gets the quantization range of specified video port.
 *
 * This function is used to get the quantization range of the specified video port.
 * For source devices, this function would return dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN when TV is not connected.
 *
 * @param[in]  handle               - Handle of the video port returned from dsGetVideoPort()
 * @param[out] quantization_range   - pointer to quantization range.  Please refer ::dsDisplayQuantizationRange_t
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    bool drmConnected = false, drmEnabled = false;
    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled) || !drmConnected) {
        *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN;
        hal_warn("HDMI not connected (DRM), quantization range UNKNOWN\n");
        return dsERR_NONE;
    }

    *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_FULL;
    hal_dbg("Quantization range defaulted to FULL\n");
    return dsERR_NONE;
}

/**
 * @brief Gets current color space setting, color depth, matrix coefficients, video Electro-Optical Transfer Function (EOT)
 *        and  quantization range in one call of the specified video port
 *
 * @param[in]  handle               - Handle of the video port returned from dsGetVideoPort()
 * @param[out] video_eotf           - pointer to EOTF value.  Please refer ::dsHDRStandard_t
 * @param[out] matrix_coefficients  - pointer to matrix coefficients value.  Please refer ::dsDisplayMatrixCoefficients_t
 * @param[out] color_space          - pointer to color space value.  Please refer ::dsDisplayColorSpace_t
 * @param[out] color_depth          - pointer to color depths value.  Please refer ::dsDisplayColorDepth_t
 * @param[out] quantization_range   - pointer to quantization range value.  Please refer ::dsDisplayQuantizationRange_t
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    bool drmConnected = false, drmEnabled = false;
    if (!drm_get_hdmi_connector_state(&drmConnected, &drmEnabled)) {
        hal_err("Failed to get HDMI connector state\n");
        return dsERR_GENERAL;
    }

    if (!drmConnected) {
        *video_eotf = dsHDRSTANDARD_SDR;
        *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
        *color_space = dsDISPLAY_COLORSPACE_UNKNOWN;
        *color_depth = dsDISPLAY_COLORDEPTH_UNKNOWN;
        *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN;
        hal_warn("HDMI not connected (DRM), output settings UNKNOWN\n");
        return dsERR_NONE;
    }

    /* RPi4 defaults in DRM-only mode */
    *video_eotf = dsHDRSTANDARD_SDR;
    *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_BT_709;
    *color_space = dsDISPLAY_COLORSPACE_RGB;
    *color_depth = dsDISPLAY_COLORDEPTH_8BIT;
    *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_FULL;

    hal_dbg("Current output settings: EOTF=%u, MatrixCoeff=%u, ColorSpace=%u, ColorDepth=0x%x, QuantRange=%u\n",
            *video_eotf, *matrix_coefficients, *color_space, *color_depth, *quantization_range);
    return dsERR_NONE;
}

/**
 * @brief Checks if video output is HDR or not.
 *
 * This function checks if the video output is HDR or not.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 * @param [out] hdr     - pointer to HDR support status.( @a true if output is HDR, @a false otherwise )
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
 * @see dsGetTVHDRCapabilities()
 *
 * @warning  This API is Not thread safe.
 */
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 HDMI output is SDR-only. VideoCore VI does not support any HDR standards
     * (HDR10, HLG, Dolby Vision, HDR10+, etc.). Output is always SDR. */
    *hdr = false;

    hal_dbg("HDR output: %s (RPi4 supports SDR only)\n", *hdr ? "true" : "false");
    return dsERR_NONE;
}

/**
 * @brief Resets Video Output to SDR.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * For source devices, this function resets the video output to SDR.
 * It forces and locks the HDMI output to SDR mode regardless of the source content format
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsVideoPortInit() must be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsResetOutputToSDR()
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    /* RPi4 HDMI output is strictly SDR-only; there is no HDR mode to reset from.
     * Forcing SDR output is not applicable on hardware that doesn't support HDR. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the preferred HDMI Protocol of the specified video port.
 *
 * This function sets the preferred HDMI Protocol of the specified video port.
 *
 * @param[in] handle                    - Handle of the video port returned from dsGetVideoPort()
 * @param[in] hdcpCurrentProtocol       - HDCP protocol to be set.  Please refer ::dsHdcpProtocolVersion_t
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
 * @see dsGetHdmiPreference()
 */
dsError_t dsSetHdmiPreference(intptr_t handle, dsHdcpProtocolVersion_t *hdcpCurrentProtocol)
{
    hal_info("invoked.\n");

    if (!_bIsVideoPortInitialized) {
        hal_err("Video port not initialized.\n");
        return dsERR_NOT_INITIALIZED;
    }

    // also verify hdcpCurrentProtocol is a valid pointer and value
    if (hdcpCurrentProtocol == NULL || !isValidVopHandle(handle)) {
        hal_err("Invalid handle (%p) or NULL hdcpCurrentProtocol (%p).\n",
                (void *)handle, (void *)hdcpCurrentProtocol);
        return dsERR_INVALID_PARAM;
    }

    if (*hdcpCurrentProtocol < dsHDCP_VERSION_1X || *hdcpCurrentProtocol >= dsHDCP_VERSION_MAX) {
        hal_err("Invalid HDCP protocol version specified: %d.\n", *hdcpCurrentProtocol);
        return dsERR_INVALID_PARAM;
    }

    /* RPi4 does not support HDCP. HDCP protection is not available on this platform. */
    hal_warn("HDCP protocol preference requested, but HDCP is not supported on RPi4.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the preferred HDMI Protocol version of specified video port.
 *
 * This function is used to get the preferred HDMI protocol version of the specified video port.
 *
 * @param[in] handle                    - Handle of the video port returned from dsGetVideoPort()
 * @param [out] hdcpCurrentProtocol     - Preferred HDMI Protocol.  Please refer ::dsHdcpProtocolVersion_t
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
 * @see dsSetHdmiPreference()
 */
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

/**
 * @brief Gets the IgnoreEDID status variable set in the device.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 * For source devices, this function is used to retrieve the status variable in order to determine whether to ignore the EDID data.
 * Used by caller to decide whether it should handle the hdmi resolution settings or not after hdcp Authentication.
 * If platform doesn't want to set the status, then returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle        - Handle of the video port returned from dsGetVideoPort()
 * @param [out] status      - Status of IgnoreEDID variable, ( @a true if EDID data can be ignored, @a false otherwise )
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 is a source device. Return the stored EDID ignore status. */
    *status = _bIgnoreEDID;

    hal_dbg("EDID ignore status: %s\n", *status ? "true" : "false");
    return dsERR_NONE;
}

/**
 * @brief Sets the background color of the specified video port.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * For source devices, this function sets the background color of the specified video port.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 * @param[in] color     - Background color to be set.  Please refer ::dsVideoBackgroundColor_t
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
dsError_t dsSetBackgroundColor(intptr_t handle, dsVideoBackgroundColor_t color)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid.\n", handle);
        return dsERR_INVALID_PARAM;
    }

    /* Validate color is in valid range (0-2); MAX is a sentinel value. */
    if (color >= dsVIDEO_BGCOLOR_MAX) {
        hal_err("Invalid background color value: %d.\n", color);
        return dsERR_INVALID_PARAM;
    }

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 VideoCore VI does not support background color control via TVService.
     * This feature is not available on this platform. */
    hal_warn("Background color setting requested, but not supported on RPi4.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets/Resets the force HDR mode.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * For source devices, this function is used to set/reset force HDR mode for the specified video port.
 * It forces and locks the HDMI output to a specified HDR mode regardless of the source content format,
 * if the mode dsHDRSTANDARD_NONE is set, then the HDMI output to follow source content format.
 *
 * @param[in] handle    - Handle of the video port returned from dsGetVideoPort()
 * @param[in] mode      - HDR mode to be forced.  Please refer ::dsHDRStandard_t
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

/**
 * @brief Gets the color depth capabilities of the specified video port
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 * For source devices, this function is used to get the color depth capabilities of the specified video port.
 *
 * @param[in] handle                    - Handle of the video port returned from dsGetVideoPort()
 * @param [out] colorDepthCapability    - OR-ed value of supported color depth standards.  Please refer ::dsDisplayColorDepth_t
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
dsError_t dsColorDepthCapabilities(intptr_t handle, unsigned int *colorDepthCapability )
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepthCapability == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or colorDepthCapability(%p) is null.\n", handle, colorDepthCapability);
        return dsERR_INVALID_PARAM;
    }

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 HDMI output is limited to 8-bit color depth across all modes and resolutions.
     * VideoCore VI does not support 10-bit or 12-bit deep color output. */
    *colorDepthCapability = dsDISPLAY_COLORDEPTH_8BIT;

    hal_dbg("Color depth capabilities: 0x%x\n", *colorDepthCapability);
    return dsERR_NONE;
}

/**
 * @brief Gets the preferred color depth values.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 * For source devices, this function is used to get the preferred color depth of the specified video port.
 * Typically for UHD resolution, the color depth is 10/12-bit, while for non-UHD resolutions, it is 8-bit.
 *
 * @param[in] handle        - Handle of the video port returned from dsGetVideoPort()
 * @param [out] colorDepth  - color depth value.  Please refer ::dsDisplayColorDepth_t
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
 * @see dsSetPreferredColorDepth()
 */
dsError_t dsGetPreferredColorDepth(intptr_t handle, dsDisplayColorDepth_t *colorDepth)
{
    hal_info("invoked.\n");
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (colorDepth == NULL || !isValidVopHandle(handle)) {
        hal_err("handle(%p) is invalid or colorDepth(%p) is null.\n", handle, colorDepth);
        return dsERR_INVALID_PARAM;
    }

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 only supports 8-bit color depth; 8BIT is both the capability and the preferred depth. */
    *colorDepth = dsDISPLAY_COLORDEPTH_8BIT;

    hal_dbg("Preferred color depth: 0x%x\n", *colorDepth);
    return dsERR_NONE;
}

/**
 * @brief Sets the preferred color depth for the specified video port.
 *
 * For sink devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always
 * For source devices, this function is used to set the preferred color depth for the specified video port.
 *
 * @param[in] handle        - Handle of the video port returned from dsGetVideoPort()
 * @param[in] colorDepth    - color depth value.Please refer :: dsDisplayColorDepth_t
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
 * @see dsGetPreferredColorDepth()
 */
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 color depth is hardware-fixed at 8-bit by VideoCore VI firmware.
     * There is no TVService API to change the output color depth on this platform. */
    hal_warn("Preferred color depth set requested, but color depth is fixed at 8-bit on RPi4.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
}
