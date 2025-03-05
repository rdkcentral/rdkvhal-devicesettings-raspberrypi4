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
#include <time.h>

#include "dsTypes.h"
#include "dsDisplay.h"
#include "dsUtl.h"
#include "dsError.h"
#include "dsVideoResolutionSettings.h"
#include "dshalLogger.h"
#include "dshalUtils.h"

#define MAX_HDMI_CODE_ID (127)
dsDisplayEventCallback_t _halcallback = NULL;
dsVideoPortResolution_t *HdmiSupportedResolution = NULL;
static unsigned int numSupportedResn = 0;
static bool _bDisplayInited = false;
static bool isBootup = true;
static dsError_t dsQueryHdmiResolution();
TV_SUPPORTED_MODE_T dsVideoPortgetVideoFormatFromInfo(dsVideoResolution_t res,
        unsigned frameRate, bool interlaced);
static dsVideoPortResolution_t *dsgetResolutionInfo(const char *res_name);

typedef struct _VDISPHandle_t {
    dsVideoPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
} VDISPHandle_t;

static VDISPHandle_t _VDispHandles[dsVIDEOPORT_TYPE_MAX][2] = {};

bool dsIsValidVDispHandle(intptr_t m_handle) {
    for (int i = 0; i < dsVIDEOPORT_TYPE_MAX; i++) {
        //hal_info("Checking if m_handle(%p) is a match - &_VDispHandles[%d][0](%p).\n", m_handle, i, &_VDispHandles[i][0]);
        if ((intptr_t)&_VDispHandles[i][0] == m_handle) {
            hal_info("m_handle(%p) is a match.\n", m_handle);
            return true;
        }
    }
    return false;
}

static void tvservice_callback(void *callback_data, uint32_t reason, uint32_t param1, uint32_t param2)
{
    VDISPHandle_t *hdmiHandle = (VDISPHandle_t*)callback_data;
    unsigned char eventData = 0;
    hal_info("Got handle %p and reason %d, param1 %d, param2 %d\n", hdmiHandle, reason, param1, param2);
    switch (reason) {
        case VC_HDMI_UNPLUGGED:
            hal_dbg("HDMI cable is unplugged\n");
            if (NULL != _halcallback) {
                _halcallback((int)(hdmiHandle->m_nativeHandle), dsDISPLAY_EVENT_DISCONNECTED, &eventData);
            } else {
                hal_warn("_halcallback is NULL, dropping event reporting.\n");
            }
            break;
        case VC_HDMI_ATTACHED:
        case VC_HDMI_DVI:
        case VC_HDMI_HDMI:
            hal_dbg("HDMI is attached\n");
            if (NULL != _halcallback) {
                _halcallback((int)(hdmiHandle->m_nativeHandle), dsDISPLAY_EVENT_CONNECTED, &eventData);
            } else {
                hal_warn("_halcallback is NULL, dropping event reporting.\n");
            }
            break;
        case VC_HDMI_HDCP_UNAUTH:
        case VC_HDMI_HDCP_AUTH:
        case VC_HDMI_HDCP_KEY_DOWNLOAD:
        case VC_HDMI_HDCP_SRM_DOWNLOAD:
            hal_warn("HDCP related events; dropping\n");
            break;
        default:
            if (isBootup == true) {
                hal_dbg("For Rpi - HDMI is attached by default\n");
                if (NULL != _halcallback) {
                    _halcallback((int)(hdmiHandle->m_nativeHandle), dsDISPLAY_EVENT_CONNECTED, &eventData);
                } else {
                    hal_warn("_halcallback is NULL, dropping event reporting.\n");
                }
                isBootup = false;
            }
            break;
    }
}

/**
 * @brief Initialize underlying Video display units
 *
 * This function must initialize all the video display units and associated data structs
 *
 * @param None
 * @return dsError_t Error Code.
 */
dsError_t dsDisplayInit()
{
    hal_info("Invoked\n");
    if (true == _bDisplayInited) {
        return dsERR_ALREADY_INITIALIZED;
    }

    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType  = dsVIDEOPORT_TYPE_HDMI;
    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = dsVIDEOPORT_TYPE_HDMI;
    _VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;

    _VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_vType  = dsVIDEOPORT_TYPE_BB;
    _VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_nativeHandle = dsVIDEOPORT_TYPE_BB;
    _VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_index = 0;

    hal_info("&_VDispHandles = %p\n", &_VDispHandles);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_vType);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0].m_index);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_vType = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_vType);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_nativeHandle = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_nativeHandle);
    hal_info("&_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_index = %p\n", &_VDispHandles[dsVIDEOPORT_TYPE_COMPONENT][0].m_index);

    int32_t res = vchi_tv_init();
    if (res != 0) {
        hal_err("vchi_tv_init failed.\n");
        return dsERR_GENERAL;
    }
    // Register callback for HDMI hotplug
    vc_tv_register_callback(&tvservice_callback, &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0]);
    /*Query the HDMI Resolution */
    dsQueryHdmiResolution();
    _bDisplayInited = true;
    return dsERR_NONE;
}

/**
 * @brief To get the handle of the video display device
 *
 * This function is used to get the display handle of a given type
 *
 * @param [in] index     The index of the display device (0, 1, ...)
 * @param [out] *handle  The handle of video display device
 * @return dsError_t Error code.
 */
dsError_t dsGetDisplay(dsVideoPortType_t m_vType, int index, intptr_t *handle)
{
    hal_info("Invoked\n");
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }

    if (index != 0 || !dsVideoPortType_isValid(m_vType) || NULL == handle) {
        hal_err("Invalid params, index %d, m_vType %d, handle %p\n", index, m_vType, handle);
        return dsERR_INVALID_PARAM;
    }

    *handle = (intptr_t)&_VDispHandles[m_vType][index];
    hal_dbg("handle = %p\n", *handle);

    return dsERR_NONE;
}

/**
 * @brief To get the aspect ration of the video display
 *
 * This function is used to get the aspect ratio that is set and used currently in
 * connected display device.
 *
 * @param [in] handle          Handle for the video display
 * @param [out] *aspectRatio   The Aspect ration that is used
 * @return dsError_t Error code.
 */
dsError_t dsGetDisplayAspectRatio(intptr_t handle, dsVideoAspectRatio_t *aspect)
{
    hal_info("Invoked\n");
    TV_DISPLAY_STATE_T tvstate;
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == aspect) {
        hal_err("Invalid params, handle %p, aspect %p\n", vDispHandle, aspect);
        return dsERR_INVALID_PARAM;
    }

    if (vc_tv_get_display_state(&tvstate) == 0) {
        if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
            hal_info("PortType:HDMI, aspect ratio is %d\n", tvstate.display.hdmi.aspect_ratio);
            switch (tvstate.display.hdmi.aspect_ratio) {
                case HDMI_ASPECT_4_3:
                    *aspect = dsVIDEO_ASPECT_RATIO_4x3;
                    break;
                case HDMI_ASPECT_16_9:
                    *aspect = dsVIDEO_ASPECT_RATIO_16x9;
                    break;
                default:
                    *aspect = dsVIDEO_ASPECT_RATIO_4x3;
                    break;
            }
        } else if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_BB) {
            hal_info("PortType:BB, aspect ratio is %d\n", tvstate.display.sdtv.display_options.aspect);
            switch (tvstate.display.sdtv.display_options.aspect) {
                case SDTV_ASPECT_4_3:
                    *aspect = dsVIDEO_ASPECT_RATIO_4x3;
                    break;
                case SDTV_ASPECT_16_9:
                    *aspect = dsVIDEO_ASPECT_RATIO_16x9;
                    break;
                default:
                    *aspect = dsVIDEO_ASPECT_RATIO_4x3;
                    break;
            }
        }
    } else {
        hal_err("Error getting current display state\n");
        return dsERR_GENERAL;
    }
    hal_dbg("Aspect ratio is %d\n", *aspect);
    return dsERR_NONE;
}

/**
 * @brief Callback registration for display related events.
 *
 * This function registers a callback for display events corresponding to
 * the specified display device handle.
 *
 * @note Caller should install at most one callback function per handle.
 * Multiple listeners are supported at Caller layer and thus not
 * required in HAL implementation.
 *
 * @param[in] handle    - Handle of the display device
 * @param[in] cb        - Display Event callback function. Please refer ::dsDisplayEventCallback_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsDisplayEventCallback_t()
 *
 */
dsError_t dsRegisterDisplayEventCallback(intptr_t handle, dsDisplayEventCallback_t cb)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    /* FIXME: RDKVREFPLT-4942 DSMgr passes handle as NULL */
    if ((handle != (intptr_t)NULL && !dsIsValidVDispHandle((intptr_t)vDispHandle)) || cb == NULL) {
        hal_err("Invalid params, cb %p, handle %p\n", cb, vDispHandle);
        return dsERR_INVALID_PARAM;
    }
    /* Register The call Back */
    if (NULL != _halcallback) {
        hal_warn("Callback already registered; override with new one.\n");
    }
    _halcallback = cb;
    return dsERR_NONE;
}

/**
 * @brief Gets the EDID information from the specified display device.
 *
 * This function gets the EDID information from the HDMI/DVI display corresponding to
 * the specified display device handle.
 *
 * @param[in]  handle   - Handle of the display device
 * @param[out] edid     - EDID info of the specified display device. Please refer ::dsDisplayEDID_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDID(intptr_t handle, dsDisplayEDID_t *edid)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == edid) {
        hal_err("Invalid params, handle %p, edid %p\n", vDispHandle, edid);
        return dsERR_INVALID_PARAM;
    }
    unsigned char *raw = (unsigned char *)calloc(MAX_EDID_BYTES_LEN, sizeof(unsigned char));
    int length = 0;
    edid->numOfSupportedResolution = 0;
    if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
        if (dsGetEDIDBytes(handle, raw, &length) != dsERR_NONE) {
            hal_err("Failed to get EDID bytes\n");
            return dsERR_GENERAL;
        }
        hal_dbg("Raw EDID debug\n");
        EDID_t parsed_edid;
        parse_edid(raw, &parsed_edid);
        print_edid(&parsed_edid);
        edid->productCode = parsed_edid.product_code;
        edid->serialNumber = parsed_edid.serial_number;
        edid->manufactureWeek = parsed_edid.week_of_manufacture;
        edid->manufactureYear = parsed_edid.year_of_manufacture;
        edid->hdmiDeviceType = true;
        edid->isRepeater = false;
        edid->physicalAddressA = 0;
        edid->physicalAddressB = 0;
        edid->physicalAddressC = 0;
        edid->physicalAddressD = 0;
        strncpy(edid->monitorName, "Unknown", sizeof(edid->monitorName));
        edid->monitorName[dsEEDID_MAX_MON_NAME_LENGTH - 1] = '\0';
        if (dsQueryHdmiResolution() != dsERR_NONE) {
            hal_err("Failed to query HDMI resolution\n");
            return dsERR_GENERAL;
        }
        hal_dbg("numSupportedResn from Table - %d\n", numSupportedResn);
        if (numSupportedResn == 0) {
            hal_err("No supported resolutions found\n");
            return dsERR_GENERAL;
        }
        for (unsigned int i = 0; i < numSupportedResn; i++) {
            memcpy(&edid->suppResolutionList[i], &HdmiSupportedResolution[i], sizeof(dsVideoPortResolution_t));
            hal_dbg("Copied resolution %s\n", edid->suppResolutionList[i].name);
        }
        edid->numOfSupportedResolution = numSupportedResn;
        if (NULL != raw) {
            free(raw);
        }
    } else {
        hal_err("Handle type %d is not supported(not dsVIDEOPORT_TYPE_HDMI)\n", vDispHandle->m_vType);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    return dsERR_NONE;
}

/**
 * @brief Terminate the usage of video display module
 *
 * This function will reset the data structs used within this module and release the video display specific handles
 *
 * @param None
 * @return dsError_t Error code.
 */
dsError_t dsDisplayTerm()
{
    hal_info("Invoked\n");
    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    vchi_tv_uninit();
    if (HdmiSupportedResolution) {
        free(HdmiSupportedResolution);
        HdmiSupportedResolution = NULL;
    }
    _bDisplayInited = false;
    return dsERR_NONE;
}

/**
 * @brief To get the native handle of the video display device
 *
 * This function is used to get the display handle of a given type
 *
 * @param [in] m_vType     Type of video display (HDMI, COMPONENT, ...)
 * @param [in] index     The index of the display device (0, 1, ...)
 * @param [out] *handle  The handle of video display device
 * @return dsError_t Error code.
 */
dsError_t dsDisplaygetNativeHandle(intptr_t handle, int *native)
{
    hal_info("Invoked\n");
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || NULL == native) {
        hal_err("Invalid params, handle %p, native %p\n", vDispHandle, native);
        return dsERR_INVALID_PARAM;
    }
    if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI && vDispHandle->m_index == 0) {
        *native = vDispHandle->m_nativeHandle;
        return dsERR_NONE;
    } else {
        hal_warn("Failed to get native handle of type %d\n", vDispHandle->m_vType);
    }
    return dsERR_GENERAL;
}


/**
 *	Get The HDMI Resolution List
 *
 **/
static dsError_t dsQueryHdmiResolution()
{
    hal_info("Invoked\n");
    static TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_CODE_ID];
    HDMI_RES_GROUP_T group;
    uint32_t mode;
    int num_of_modes;
    memset(modeSupported, 0, sizeof(modeSupported));

    num_of_modes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA, modeSupported,
            vcos_countof(modeSupported),
            &group,
            &mode);
    if (num_of_modes < 0) {
        hal_err("Failed to get modes vc_tv_hdmi_get_supported_modes_new\n");
        return dsERR_GENERAL;
    }
    if (HdmiSupportedResolution) {
        free(HdmiSupportedResolution);
        HdmiSupportedResolution = NULL;
    }
    numSupportedResn = 0;
    HdmiSupportedResolution = (dsVideoPortResolution_t *)malloc(sizeof(dsVideoPortResolution_t)*noOfItemsInResolutionMap);
    if (HdmiSupportedResolution) {
        for (size_t i = 0; i < noOfItemsInResolutionMap; i++) {
            for (int j = 0; j < num_of_modes; j++) {
                if (modeSupported[j].code == resolutionMap[i].mode) {
                    dsVideoPortResolution_t *resolution = dsgetResolutionInfo(resolutionMap[i].rdkRes);
                    memcpy(&HdmiSupportedResolution[numSupportedResn], resolution, sizeof(dsVideoPortResolution_t));
                    hal_dbg("Supported Resolution '%s'\n", HdmiSupportedResolution[numSupportedResn].name);
                    numSupportedResn++;
                }
            }
        }
    } else {
        hal_err("malloc failed\n");
        return dsERR_GENERAL;
    }
    hal_dbg("Total Device supported resolutions on HDMI = %d\n", numSupportedResn);
    return dsERR_NONE;
}

static dsVideoPortResolution_t* dsgetResolutionInfo(const char *res_name)
{
    hal_info("Invoked\n");
    size_t iCount = 0;
    iCount = (sizeof(kResolutions) / sizeof(kResolutions[0]));
    for (size_t i=0; i < iCount; i++) {
        if (!strncmp(res_name, kResolutions[i].name, strlen(res_name))) {
            return &kResolutions[i];
        }
    }
    return NULL;
}

TV_SUPPORTED_MODE_T dsVideoPortgetVideoFormatFromInfo(dsVideoResolution_t res, unsigned frameRate, bool interlaced)
{
    hal_info("Invoked\n");
    TV_SUPPORTED_MODE_T format = {0};
    switch (res) {
        case dsVIDEO_PIXELRES_720x480:
            format.height = 480;
            break;
        case dsVIDEO_PIXELRES_720x576:
            format.height = 576;
            break;
        case dsVIDEO_PIXELRES_1280x720:
            format.height = 720;
            break;
        case dsVIDEO_PIXELRES_1366x768:
            format.height = 768;
            break;
        case dsVIDEO_PIXELRES_1920x1080:
            format.height = 1080;
            break;
        case dsVIDEO_PIXELRES_3840x2160:
            format.height = 2160;
            break;
        case dsVIDEO_PIXELRES_4096x2160:
            format.height = 2160;
            break;
        case dsVIDEO_PIXELRES_MAX:
        default:
            break;
    }

    switch (frameRate) {
        case dsVIDEO_FRAMERATE_24:
            format.frame_rate = 24;
            break;
        case dsVIDEO_FRAMERATE_25:
            format.frame_rate = 25;
            break;
        case dsVIDEO_FRAMERATE_30:
            format.frame_rate = 30;
            break;
        case dsVIDEO_FRAMERATE_60:
            format.frame_rate = 60;
            break;
        case dsVIDEO_FRAMERATE_23dot98:
            format.frame_rate = 23.98;
            break;
        case dsVIDEO_FRAMERATE_29dot97:
            format.frame_rate = 29.97;
            break;
        case dsVIDEO_FRAMERATE_50:
            format.frame_rate = 50;
            break;
        case dsVIDEO_FRAMERATE_59dot94:
            format.frame_rate = 59.94;
            break;
        case dsVIDEO_FRAMERATE_MAX:
        case dsVIDEO_FRAMERATE_UNKNOWN:
        default:
            break;
    }
    if (interlaced) {
        format.scan_mode = 1; // Interlaced
    } else {
        format.scan_mode = 0; // Progressive
    }
    return format;
}

/**
 * @brief Gets the EDID buffer and EDID length of connected display device.
 *
 * This function is used to get the EDID buffer and EDID size of the connected display corresponding to
 * the specified display device handle.
 *
 * @param[in] handle    - Handle of the display device
 * @param[out] edid     - Pointer to raw EDID buffer
 * @param[out] length   - length of the EDID buffer data. Min value is 0
 *
 * @note Caller is responsible for allocating memory for edid( please refer ::MAX_EDID_BYTES_LEN ) and freeing the EDID buffer
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDIDBytes(intptr_t handle, unsigned char *edid, int *length)
{
    hal_info("Invoked\n");
    uint8_t buffer[128] = {0};
    size_t offset = 0;
    int i, extensions = 0;
    VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

    if (false == _bDisplayInited) {
        return dsERR_NOT_INITIALIZED;
    }
    if (edid == NULL || length == NULL) {
        hal_err("invalid params\n");
        return dsERR_INVALID_PARAM;
    } else if (!dsIsValidVDispHandle((intptr_t)vDispHandle) || vDispHandle != &_VDispHandles[dsVIDEOPORT_TYPE_HDMI][0]) {
        hal_err("invalid handle\n");
        return dsERR_INVALID_PARAM;
    }

    *length = 0;
    int siz = vc_tv_hdmi_ddc_read(offset, sizeof (buffer), buffer);
    if (siz <= 0) {
        hal_err("vc_tv_hdmi_ddc_read returned %d.\n", siz);
        return dsERR_GENERAL;
    }
    offset += sizeof( buffer);
    extensions = buffer[0x7e]; /* This tells you how many more blocks to read */
    memcpy(edid, (unsigned char *)buffer, sizeof(buffer));
    /* First block always exist */
    for (i = 0; i < extensions; i++, offset += sizeof( buffer)) {
        memset(buffer, 0, sizeof(buffer));
        siz = vc_tv_hdmi_ddc_read(offset, sizeof(buffer), buffer);
        if (siz <= 0) {
            hal_err("subsequent vc_tv_hdmi_ddc_read returned %d.\n", siz);
            return dsERR_GENERAL;
        }
        memcpy(edid + offset, buffer, sizeof(buffer));
    }
    *length = offset;
#if 1 // Print EDID bytes for debugging
    FILE *file = fopen("/tmp/.hal-edid-bytes.dat", "wb");
    if (file != NULL) {
        for (i = 0; i < *length; i++) {
            fprintf(file, "%02x", edid[i]);
        }
        fclose(file);
        hal_info("EDID bytes written to /tmp/.hal-edid-bytes.dat\n");
    } else {
        hal_err("Failed to open /tmp/.hal-edid-bytes.dat\n");
    }
#endif
    return dsERR_NONE;
}
