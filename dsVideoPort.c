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
#include "dshalUtils.h"
#include "dshalLogger.h"
#include "interface/vmcs_host/vc_hdmi.h"

static bool isBootup = true;
static bool _bIsVideoPortInitialized = false;
static bool isValidVopHandle(intptr_t handle);
static const char *dsVideoGetResolution(uint32_t mode);
static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution);
#define MAX_HDMI_MODE_ID (127)

/* EDID and CTA-861 related constants */

#define EDID_BLOCK_SIZE                        128
#define EDID_MAX_BLOCKS                        4
#define EDID_BUFFER_SIZE                        (EDID_BLOCK_SIZE * EDID_MAX_BLOCKS)

#define EDID_NUM_EXTENSIONS_OFFSET             126

#define EDID_CTA_DTD_OFFSET_INDEX              2
#define EDID_CTA_DATA_BLOCK_COLLECTION_START   4
#define EDID_CTA_MAX_OFFSET                    (EDID_BLOCK_SIZE - 1)

#define EDID_CTA_DATA_BLOCK_TAG_MASK           0xE0
#define EDID_CTA_DATA_BLOCK_LEN_MASK           0x1F

/* CTA extension tags */
#define EDID_CTA_EXTENSION_TAG                 0x02
#define EDID_CTA_EXTENDED_TAG                  0x07
#define EDID_CTA_VENDOR_SPECIFIC_TAG           0x03

/* CTA extended data block tag */
#define EDID_EXT_TAG_HDR_STATIC_METADATA       0x06

/* HDR EOTF flags */
#define EDID_EOTF_HDR10_BIT                    0x04
#define EDID_EOTF_HLG_BIT                      0x08

/* Dolby Vision VSIF IEEE Registration Identifier (OUI) */
#define EDID_DOLBY_VSIF_OUI_BYTE0              0x46
#define EDID_DOLBY_VSIF_OUI_BYTE1              0xD0
#define EDID_DOLBY_VSIF_OUI_BYTE2              0x00

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

static bool read_sysfs_line(const char *path, char *buf, size_t len)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return false;
    }

    if (fgets(buf, (int)len, fp) == NULL) {
        fclose(fp);
        return false;
    }

    fclose(fp);
    buf[strcspn(buf, "\r\n")] = '\0';
    return true;
}

static void resolve_drm_card_name(char *cardName, size_t len)
{
    const char *cardPath = getenv("WESTEROS_DRM_CARD");
    if (cardPath == NULL || cardPath[0] == '\0') {
        cardPath = DRI_CARD;
    }

    const char *slash = strrchr(cardPath, '/');
    const char *base = (slash != NULL) ? (slash + 1) : cardPath;

    snprintf(cardName, len, "%s", base);
}

static bool drm_get_hdmi_connector_state(bool *connected, bool *enabled)
{
    DIR *dir;
    struct dirent *entry;
    char cardName[32] = {0};
    char statusPath[PATH_MAX] = {0};
    char enabledPath[PATH_MAX] = {0};
    char line[64] = {0};
    bool found = false;

    if (connected == NULL || enabled == NULL) {
        return false;
    }

    resolve_drm_card_name(cardName, sizeof(cardName));

    dir = opendir("/sys/class/drm");
    if (dir == NULL) {
        return false;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }

        if (strncmp(entry->d_name, cardName, strlen(cardName)) != 0) {
            continue;
        }

        if (strstr(entry->d_name, "HDMI-A-") == NULL) {
            continue;
        }

        snprintf(statusPath, sizeof(statusPath), "/sys/class/drm/%s/status", entry->d_name);
        if (!read_sysfs_line(statusPath, line, sizeof(line))) {
            continue;
        }

        *connected = (strcmp(line, "connected") == 0);

        snprintf(enabledPath, sizeof(enabledPath), "/sys/class/drm/%s/enabled", entry->d_name);
        if (read_sysfs_line(enabledPath, line, sizeof(line))) {
            *enabled = (strcmp(line, "enabled") == 0);
        } else {
            *enabled = false;
        }

        found = true;
        if (*connected) {
            break;
        }
    }

    closedir(dir);
    return found;
}

static void populate_output_settings_from_tvstate(const TV_DISPLAY_STATE_T *tvstate,
        dsDisplayMatrixCoefficients_t *matrix_coefficients,
        dsDisplayColorSpace_t *color_space,
        unsigned int *color_depth,
        dsDisplayQuantizationRange_t *quantization_range)
{
    uint16_t pixel_encoding = tvstate->display.hdmi.pixel_encoding;
    uint32_t height = tvstate->display.hdmi.height;
    bool known_pixel_encoding = (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_LIMITED ||
        pixel_encoding == HDMI_PIXEL_ENCODING_RGB_FULL ||
        pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_LIMITED ||
        pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_FULL ||
        pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_LIMITED ||
        pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_FULL);

    if (!known_pixel_encoding) {
        hal_warn("Unknown pixel encoding %u in output settings mapping.\n", pixel_encoding);
    }

    if (matrix_coefficients != NULL) {
        if (pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_LIMITED ||
            pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_FULL ||
            pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_LIMITED ||
            pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_FULL) {
            *matrix_coefficients = (height >= 720)
                ? dsDISPLAY_MATRIXCOEFFICIENT_BT_709
                : dsDISPLAY_MATRIXCOEFFICIENT_BT_470_2_BG;
        }
        else if (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_LIMITED) {
            *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_eHDMI_RGB;
        }
        else if (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_FULL) {
            *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_eDVI_FR_RGB;
        }
        else {
            *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_BT_709;
        }
    }

    if (color_space != NULL) {
        if (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_LIMITED ||
            pixel_encoding == HDMI_PIXEL_ENCODING_RGB_FULL) {
            *color_space = dsDISPLAY_COLORSPACE_RGB;
        }
        else if (pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_LIMITED ||
                 pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_FULL) {
            *color_space = dsDISPLAY_COLORSPACE_YCbCr422;
        }
        else if (pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_LIMITED ||
                 pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_FULL) {
            *color_space = dsDISPLAY_COLORSPACE_YCbCr444;
        }
        else {
            *color_space = dsDISPLAY_COLORSPACE_UNKNOWN;
        }
    }

    if (color_depth != NULL) {
        *color_depth = dsDISPLAY_COLORDEPTH_8BIT;
    }

    if (quantization_range != NULL) {
        if (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_LIMITED ||
            pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_LIMITED ||
            pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_LIMITED) {
            *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_LIMITED;
        }
        else if (pixel_encoding == HDMI_PIXEL_ENCODING_RGB_FULL ||
                 pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr422_FULL ||
                 pixel_encoding == HDMI_PIXEL_ENCODING_YCbCr444_FULL) {
            *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_FULL;
        }
        else {
            *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN;
        }
    }
}

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
    _resolution = kResolutions[kDefaultResIndex];
    int rc = tvsvc_acquire();
    if (rc != 0) {
        hal_err("Failed to acquire TVService\n");
        return dsERR_GENERAL;
    }
    /*
     *  Register callback for HDCP Auth
     */
    rc = tvsvc_client_register_callback((tvsvc_client_cb_t)tvservice_hdcp_callback,
                                        &_vopHandles[dsVIDEOPORT_TYPE_HDMI][0]);
    if (rc != 0) {
        hal_err("Failed to register TVService HDCP callback, rc=%d\n", rc);
        tvsvc_release();
        return dsERR_GENERAL;
    }

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
        char cmd[256] = {0};
        char resp[256] = {0};
        const char *xdgRuntimeDir = getXDGRuntimeDir();

        if (xdgRuntimeDir == NULL) {
            hal_err("Failed to get XDG_RUNTIME_DIR\n");
            return dsERR_GENERAL;
        }

        if ((strlen("export XDG_RUNTIME_DIR=") + strlen(xdgRuntimeDir) + strlen("; westeros-gl-console set display enable ") + 3) > (sizeof(cmd) - 1)) {
            hal_err("Command buffer is too small\n");
            return dsERR_GENERAL;
        }

        snprintf(cmd, sizeof(cmd), "export XDG_RUNTIME_DIR=%s; westeros-gl-console set display enable %d", xdgRuntimeDir, enabled);
        if (!westerosRWWrapper(cmd, resp, sizeof(resp))) {
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
    const char *resolution_name = NULL;
    TV_DISPLAY_STATE_T tvstate;
    uint32_t hdmi_mode;
    if (false == _bIsVideoPortInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!isValidVopHandle(handle) || NULL == resolution) {
        hal_err("handle(%p) is invalid or resolution(%p) is NULL.\n", handle, resolution);
        return dsERR_INVALID_PARAM;
    }
    if (tvsvc_client_get_display_state(&tvstate) == 0) {
        hal_dbg("tvsvc_client_get_display_state: 0x%X\n", tvstate.state);
        if (tvstate.state & VC_HDMI_ATTACHED) {
            hal_dbg("  Width: %d\n", tvstate.display.hdmi.width);
            hal_dbg("  Height: %d\n", tvstate.display.hdmi.height);
            hal_dbg("  Frame Rate: %d\n",
                    tvstate.display.hdmi.frame_rate);
            hal_dbg("  Scan Mode: %s\n",
                    tvstate.display.hdmi.scan_mode ? "Interlaced"
                    : "Progressive");
            hal_dbg("  Aspect Ratio: %d\n",
                    tvstate.display.hdmi.aspect_ratio);
            hal_dbg("  Pixel Repetition: %d\n",
                    tvstate.display.hdmi.pixel_rep);
            hal_dbg("  Group: %d\n", tvstate.display.hdmi.group);
            hal_dbg("  Mode: %d\n", tvstate.display.hdmi.mode);
        } else {
            hal_err("HDMI not connected.\n");
        }
        resolution_name = dsVideoGetResolution(tvstate.display.hdmi.mode);
    }
    if (resolution_name == NULL) {
        hdmi_mode = dsGetHdmiMode(resolution);
        resolution_name = dsVideoGetResolution(hdmi_mode);
    }
    if (resolution_name)
        strncpy(resolution->name, resolution_name, strlen(resolution_name));
    return dsERR_NONE;
}

static const char* dsVideoGetResolution(uint32_t hdmiMode)
{
    hal_info("invoked.\n");
    char resName[32] = {'\0'};
    const char *resolution_name = NULL;
    char cmdBuf[256] = {'\0'};
    snprintf(cmdBuf, sizeof(cmdBuf)-1,
             "export XDG_RUNTIME_DIR=%s; westeros-gl-console get mode",
             XDG_RUNTIME_DIR);

    FILE *fp = popen(cmdBuf, "r");
    if (fp == NULL) {
        printf("DS_HAL: popen failed\n");
        return NULL;
    }

    char output[128] = {'\0'};
    while (fgets(output, sizeof(output)-1, fp)) {
        int width=-1, height=-1, rate=60;

        if (strstr(output, "Response: [0:")) {

            if (sscanf(output, "Response: [0: mode %dx%dp%d]", &width, &height, &rate) == 3) {
                snprintf(resName, sizeof(resName), "%dp%d", height, rate);
                break;
            }
            else if (sscanf(output, "Response: [0: mode %dx%di%d]", &width, &height, &rate) == 3) {
                snprintf(resName, sizeof(resName), "%di%d", height, rate);
                break;
            }
            else if (sscanf(output, "Response: [0: mode %dx%d]", &width, &height) == 2) {
                rate = 60; // default
                snprintf(resName, sizeof(resName), "%dp%d", height, rate);
                break;
            }
        }
    }

    pclose(fp);

    hal_info("resName %s\n", resName);

    for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
        const char *mapRes = resolutionMap[i].rdkRes;

        size_t len = strlen(mapRes);
        int hasRate = (len > 0 && isdigit((unsigned char)mapRes[len-1]));

        if (hasRate) {
            if (strcmp(mapRes, resName) == 0) {
                resolution_name = mapRes;
                break;
            }
        } else {
            char temp[32];
            snprintf(temp,sizeof(temp), "%s60", mapRes);

            if (strcmp(temp, resName) == 0) {
                resolution_name = mapRes;
                break;
            }
        }
    }

    hal_info("resolution_name %s\n", resolution_name);

    return resolution_name;
}

static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution)
{
    hal_info("invoked.\n");
    uint32_t hdmi_mode = 0;
    for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
        size_t length = strlen(resolution->name) > strlen(resolutionMap[i].rdkRes) ? strlen(resolution->name) : strlen(resolutionMap[i].rdkRes);
        if (!strncmp(resolution->name, resolutionMap[i].rdkRes, length))
        {
            hdmi_mode = resolutionMap[i].mode;
            break;
        }
    }
    if (!hdmi_mode) {
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
        char cmdBuf[512] = {'\0'};
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
        //extended command to make resolution setting more synchronous
        snprintf(cmdBuf, sizeof(cmdBuf)-1, "export XDG_RUNTIME_DIR=%s;westeros-gl-console set mode %dx%d%c%d && westeros-gl-console get mode | grep \"Response\"",
                XDG_RUNTIME_DIR,width,height,interlaced,rate);
        hal_dbg("Executing '%s'\n", cmdBuf);

        FILE* fp = popen(cmdBuf, "r");
        bool success = false;
        if (NULL != fp) {
            char output[64] = {'\0'};
            while (fgets(output, sizeof(output)-1, fp)) {
                if (strlen(output) && strstr(output, "[0: set")) {
                    success = true;
                    break;
                }
            }
            pclose(fp);
            return success ? dsERR_NONE : dsERR_GENERAL;
        } else {
            hal_err("DS_HAL: popen failed\n");
            return dsERR_GENERAL;
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
    tvsvc_client_unregister_callback((tvsvc_client_cb_t)tvservice_hdcp_callback);
    tvsvc_release();
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
        if (tvsvc_client_get_display_state( &tvstate ) == 0) {
            hal_dbg("tvsvc_client_get_display_state: 0x%x\n", tvstate.state);
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

    VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
    if (vopHandle->m_vType != dsVIDEOPORT_TYPE_HDMI) {
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    *capabilities = dsHDRSTANDARD_SDR;

    bool isConnected = false;
    dsError_t connRet = dsIsDisplayConnected(handle, &isConnected);
    if (connRet != dsERR_NONE || !isConnected) {
        return dsERR_NONE;
    }

    uint8_t edid[EDID_BUFFER_SIZE]={0};

    int readLen = tvsvc_client_ddc_read(0, EDID_BLOCK_SIZE, edid);
    if (readLen != EDID_BLOCK_SIZE) {
        hal_warn("Failed to read EDID base block, len=%d; reporting default SDR capability.\n", readLen);
        return dsERR_NONE;
    }

    uint8_t numExtensions = edid[EDID_NUM_EXTENSIONS_OFFSET];
    size_t maxExtensions = (sizeof(edid) / EDID_BLOCK_SIZE) - 1;
    size_t blocksRead = 1;

    for (size_t ext = 1; ext <= numExtensions && ext <= maxExtensions; ++ext) {
        readLen = tvsvc_client_ddc_read((uint32_t)(ext * EDID_BLOCK_SIZE), EDID_BLOCK_SIZE, edid + (ext * EDID_BLOCK_SIZE));
        if (readLen != EDID_BLOCK_SIZE) {
            hal_warn("Failed to read EDID extension block %zu, len=%d\n", ext, readLen);
            break;
        }
        blocksRead++;
    }

    bool hdrBlockFound = false;
    for (size_t block = 1; block < blocksRead; ++block) {
        uint8_t *ext = edid + (block * EDID_BLOCK_SIZE);

        if (ext[0] != EDID_CTA_EXTENSION_TAG) {
            continue;
        }

        uint8_t dtdOffset = ext[EDID_CTA_DTD_OFFSET_INDEX];
        if (dtdOffset == 0) {
            /* Per CTA-861, dtdOffset == 0 means no Data Block Collection/DTDs in this extension. */
            continue;
        }
        uint8_t dataBlockEnd = (dtdOffset > EDID_CTA_MAX_OFFSET) ? EDID_CTA_MAX_OFFSET : dtdOffset;
        if (dataBlockEnd <= EDID_CTA_DATA_BLOCK_COLLECTION_START) {
            continue;
        }

        uint8_t pos = EDID_CTA_DATA_BLOCK_COLLECTION_START;
        while (pos < dataBlockEnd) {
            uint8_t header = ext[pos];
            uint8_t tagCode = (header & EDID_CTA_DATA_BLOCK_TAG_MASK) >> 5;
            uint8_t blockLen = header & EDID_CTA_DATA_BLOCK_LEN_MASK;
            uint8_t nextPos = (uint8_t)(pos + 1 + blockLen);

            if (nextPos <= pos || nextPos > dataBlockEnd || nextPos > EDID_CTA_MAX_OFFSET) {
                hal_err("Malformed CTA data block at pos %u\n", pos);
                break;
            }

            if (tagCode == EDID_CTA_EXTENDED_TAG && blockLen >= 3) {
                uint8_t extTag = ext[pos + 1];
                if (extTag == EDID_EXT_TAG_HDR_STATIC_METADATA) {
                    uint8_t eotf = ext[pos + 2];
                    if (eotf & EDID_EOTF_HDR10_BIT) {
                        *capabilities |= dsHDRSTANDARD_HDR10;
                    }
                    if (eotf & EDID_EOTF_HLG_BIT) {
                        *capabilities |= dsHDRSTANDARD_HLG;
                    }
                    hdrBlockFound = true;
                }
            } else if (tagCode == EDID_CTA_VENDOR_SPECIFIC_TAG && blockLen >= 3) {
                if (ext[pos + 1] == EDID_DOLBY_VSIF_OUI_BYTE0 &&
                    ext[pos + 2] == EDID_DOLBY_VSIF_OUI_BYTE1 &&
                    ext[pos + 3] == EDID_DOLBY_VSIF_OUI_BYTE2) {
                    *capabilities |= dsHDRSTANDARD_DolbyVision;
                }
            }

            pos = nextPos;
        }
    }

    if (!hdrBlockFound && !(*capabilities & dsHDRSTANDARD_DolbyVision)) {
        hal_info("No CTA HDR Static Metadata block or known HDR capabilities found; reporting default SDR capability.\n");
    }

    hal_info("TV HDR capabilities=0x%x\n", *capabilities);
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

    hal_info("handle = %p and *handle = %p\n", vopHandle, *vopHandle);
    if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_MODE_ID];
        HDMI_RES_GROUP_T group;
        uint32_t mode;
        int num_of_modes;
        int i;
        const dsTVResolution_t *TVVideoResolution = NULL;
        num_of_modes = tvsvc_client_get_supported_modes(HDMI_RES_GROUP_CEA, modeSupported,
                vcos_countof(modeSupported),
                &group,
                &mode);
        if (num_of_modes < 0) {
            hal_err("Failed to get modes tvsvc_client_get_supported_modes: rc=%d\n", num_of_modes);
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
            } else {
                hal_warn("Unsupported VIC %u, skipping\n", modeSupported[i].code);
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
     * Check the current audio output: amixer cget numid=3
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

    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for matrix coefficient inference.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine matrix coefficients.\n");
        *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
        return dsERR_NONE;
    }

    populate_output_settings_from_tvstate(&tvstate, matrix_coefficients, NULL, NULL, NULL);

    hal_dbg("Matrix coefficient determined: %u\n", *matrix_coefficients);
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

    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for color depth determination.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine color depth.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* RPi4 HDMI output is limited to 8-bit color depth across all resolutions.
     * While RPi4 technically supports 4K@30Hz, all modes use 8-bit SDR (no 10/12-bit deep color). */
    *color_depth = dsDISPLAY_COLORDEPTH_8BIT;

    hal_dbg("Color depth determined: 0x%x (resolution: %ux%u)\n",
            *color_depth, tvstate.display.hdmi.width, tvstate.display.hdmi.height);
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

    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for color space determination.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine color space.\n");
        *color_space = dsDISPLAY_COLORSPACE_UNKNOWN;
        return dsERR_NONE;
    }

    populate_output_settings_from_tvstate(&tvstate, NULL, color_space, NULL, NULL);

    hal_dbg("Color space determined: %u\n", *color_space);
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

    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for quantization range determination.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine quantization range.\n");
        *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN;
        return dsERR_NONE;
    }

    populate_output_settings_from_tvstate(&tvstate, NULL, NULL, NULL, quantization_range);

    hal_dbg("Quantization range determined: %u\n", *quantization_range);
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

    /* Query display state once and reuse for all output settings determinations.
     * This is more efficient than calling individual getter functions which would
     * query TVService multiple times. */
    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for output settings determination.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine output settings.\n");
        *video_eotf = dsHDRSTANDARD_SDR;
        *matrix_coefficients = dsDISPLAY_MATRIXCOEFFICIENT_UNKNOWN;
        *color_space = dsDISPLAY_COLORSPACE_UNKNOWN;
        *color_depth = dsDISPLAY_COLORDEPTH_8BIT;
        *quantization_range = dsDISPLAY_QUANTIZATIONRANGE_UNKNOWN;
        return dsERR_NONE;
    }

    /* Video EOTF: RPi4 only supports SDR. */
    *video_eotf = dsHDRSTANDARD_SDR;

    populate_output_settings_from_tvstate(&tvstate, matrix_coefficients, color_space,
            color_depth, quantization_range);

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

    TV_DISPLAY_STATE_T tvstate;
    if (tvsvc_client_get_display_state(&tvstate) != 0) {
        hal_err("Failed to get display state for HDR check.\n");
        return dsERR_GENERAL;
    }

    if (!(tvstate.state & VC_HDMI_ATTACHED)) {
        hal_warn("HDMI not attached; cannot determine HDR support.\n");
        *hdr = false;
        return dsERR_NONE;
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
