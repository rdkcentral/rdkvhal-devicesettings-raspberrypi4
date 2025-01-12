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

#include "dsDisplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dsError.h"
#include "dsTypes.h"
#include "dsUtl.h"
#include "dsVideoResolutionSettings.h"
#include "dshalLogger.h"
#include "dshalUtils.h"

#ifdef USE_NEW_IMPLEMENTATION
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "libdrm_wrapper.h"
#include "udev_wrapper.h"

pthread_t thread_udevmon;

void *hdmi_status_change_handler(const char *devnode) {
	if ((NULL != _halcallback) && (NULL != devnode)) {
		/* Check the hdmi connection status and invoke CB */
		const char *card = strrchr(devnode, '/');
		if (!card) {
			hal_err("Invalid device node: %s\n", devnode);
			return;
		}
		card++;
		char card_path[32] = {0};
		snprintf(card_path, sizeof(card_path), "/dev/dri/%s", card);
		hal_dbg("Opening DRM device: %s\n", card_path);
		int fd = open_drm_device(card_path, DRM_NODE_PRIMARY);
		if (fd < 0) {
			hal_err("Failed to open DRM device: %s\n", card_path);
			return;
		}
		drmModeRes *resources = drmModeGetResources(fd);
		if (!resources) {
			hal_err("Failed to get DRM resources\n");
			close_drm_device(fd);
			return;
		}
		for (int i = 0; i < resources->count_connectors; i++) {
			drmModeConnector *connector =
			    drmModeGetConnector(fd, resources->connectors[i]);
			if (!connector) {
				hal_err("Failed to get connector\n");
				continue;
			}
			if (connector->connector_type ==
			        DRM_MODE_CONNECTOR_HDMIA ||
			    connector->connector_type ==
			        DRM_MODE_CONNECTOR_HDMIB) {
				const char *status = (connector->connection ==
				                      DRM_MODE_CONNECTED)
				                         ? "connected"
				                         : "disconnected";
				hal_dbg("HDMI connector %d status: %s\n",
				        connector->connector_id, status);
				_halcallback((int)(hdmiHandle->m_nativeHandle),
				             (connector->connection ==
				              DRM_MODE_CONNECTED)
				                 ? dsDISPLAY_EVENT_CONNECTED
				                 : dsDISPLAY_EVENT_DISCONNECTED,
				             NULL);
			}
			drmModeFreeConnector(connector);
		}
		drmModeFreeResources(resources);
		close_drm_device(fd);
	}
}
#endif /* USE_NEW_IMPLEMENTATION */

#define MAX_HDMI_CODE_ID (127)
dsDisplayEventCallback_t _halcallback = NULL;
dsVideoPortResolution_t *HdmiSupportedResolution = NULL;
static unsigned int numSupportedResn = 0;
static bool _bDisplayInited = false;
static bool isBootup = true;
static dsError_t dsQueryHdmiResolution();
TV_SUPPORTED_MODE_T dsVideoPortgetVideoFormatFromInfo(dsVideoResolution_t res,
                                                      unsigned frameRate,
                                                      bool interlaced);
static dsVideoPortResolution_t *dsgetResolutionInfo(const char *res_name);

typedef struct _VDISPHandle_t {
	dsVideoPortType_t m_vType;
	int m_index;
	int m_nativeHandle;
} VDISPHandle_t;

static VDISPHandle_t _handles[dsVIDEOPORT_TYPE_MAX][2] = {};

bool dsIsValidHandle(intptr_t m_handle) {
	for (int i = 0; i < dsVIDEOPORT_TYPE_MAX; i++) {
		if ((intptr_t)&_handles[i][0] == m_handle) {
			return true;
		}
	}
	return false;
}

static void tvservice_callback(void *callback_data, uint32_t reason,
                               uint32_t param1, uint32_t param2) {
	hal_dbg("invoked.\n");
	VDISPHandle_t *hdmiHandle = (VDISPHandle_t *)callback_data;
	unsigned char eventData = 0;
	switch (reason) {
		case VC_HDMI_UNPLUGGED: {
			hal_dbg("HDMI cable is unplugged\n");
			_halcallback((int)(hdmiHandle->m_nativeHandle),
			             dsDISPLAY_EVENT_DISCONNECTED, &eventData);
			break;
		}
		case VC_HDMI_ATTACHED: {
			hal_dbg("HDMI cable is attached\n");
			_halcallback((int)(hdmiHandle->m_nativeHandle),
			             dsDISPLAY_EVENT_CONNECTED, &eventData);
			break;
		}
		default: {
			if (isBootup == true) {
				hal_dbg(
				    "For Rpi - HDMI is attached by default\n");
				_halcallback((int)(hdmiHandle->m_nativeHandle),
				             dsDISPLAY_EVENT_CONNECTED,
				             &eventData);
				isBootup = false;
			}
			break;
		}
	}
}

/**
 * @brief Initializes the DS Display sub-system.
 *
 * This function initializes all required resources for Display sub-system and
 * is required to be called before the other APIs in this module. Also this
 * function needs to initialize all the required device handles for the
 * different display ports and the number of connected devices for each display
 * port.
 *
 * @return dsError_t                    - Status
 * @retval dsERR_NONE                   - Success
 * @retval dsERR_ALREADY_INITIALIZED    - Function is already initialized
 * @retval dsERR_GENERAL                - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe
 *
 * @see dsDisplayTerm()
 *
 */
dsError_t dsDisplayInit() {
	hal_dbg("invoked.\n");
	int32_t res = 0;
	if (true == _bDisplayInited) {
		return dsERR_ALREADY_INITIALIZED;
	}

	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_vType = dsVIDEOPORT_TYPE_HDMI;
	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle =
	    dsVIDEOPORT_TYPE_HDMI;
	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;

	res = vchi_tv_init();
	if (res != 0) {
		hal_err("vchi_tv_init failed.\n");
		return dsERR_GENERAL;
	}
	// Register callback for HDMI hotplug
	vc_tv_register_callback(&tvservice_callback,
	                        &_handles[dsVIDEOPORT_TYPE_HDMI][0]);
	/*Query the HDMI Resolution */
#ifdef USE_NEW_IMPLEMENTATION
	if (pthread_create(&thread_udevmon, NULL, monitor_hdmi_status_changes,
	                   (void *)hdmi_status_change_handler) != 0) {
		hal_err(
		    "monitor_hdmi_status_changes thread creation failed.\n");
		return dsERR_GENERAL;
	} else {
		hal_dbg(
		    "monitor_hdmi_status_changes thread creation success.\n");
	}
#endif /* USE_NEW_IMPLEMENTATION */
	if (dsQueryHdmiResolution() != dsERR_NONE) {
		hal_err("dsQueryHdmiResolution failed.\n");
		return dsERR_GENERAL;
	}
	_bDisplayInited = true;
	return dsERR_NONE;
}

/**
 * @brief Gets the handle of connected display device.
 *
 * This function is used to get the handle(as created in dsDisplayInit()) for
 * the connected display device corresponding to the specified video port.
 *
 * @param[in]  vType    - Type of video port. Please refer ::dsVideoPortType_t
 * @param[in]  index    - Index of the video port. (Index of the port must be 0
 * if not specified) Max index is platform specific. Min value is 0.
 * @param[out] handle   - Pointer to hold the handle of display device
 * corresponding to specified video port
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetDisplay(dsVideoPortType_t m_vType, int index, intptr_t *handle) {
	hal_dbg("invoked.\n");
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}

	if (index != 0 || !dsVideoPortType_isValid(m_vType) || NULL == handle) {
		return dsERR_INVALID_PARAM;
	}

	*handle = (intptr_t)&_handles[m_vType][index];

	return dsERR_NONE;
}

/**
 * @brief Gets the EDID information from the specified display device.
 *
 * This function gets the EDID information from the HDMI/DVI display
 * corresponding to the specified display device handle.
 *
 * @param[in]  handle   - Handle of the display device
 * @param[out] edid     - EDID info of the specified display device. Please
 * refer ::dsDisplayEDID_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this
 * API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDID(intptr_t handle, dsDisplayEDID_t *edid) {
	hal_dbg("invoked.\n");
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}

	if (vDispHandle == NULL || NULL == edid) {
		return dsERR_INVALID_PARAM;
	}
	unsigned char *raw = NULL;
	int length = 0;
	edid->numOfSupportedResolution = 0;
	if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
		if (dsGetEDIDBytes(handle, raw, &length) != dsERR_NONE) {
			hal_err("dsGetEDIDBytes failed.\n");
			return dsERR_GENERAL;
		}
		if (fill_edid_struct(raw, edid, length) != 0) {
			hal_err("fill_edid_struct failed.\n");
			return dsERR_GENERAL;
		}
		dsQueryHdmiResolution();
		hal_dbg("No. of supported resolutions: %d\n", numSupportedResn);
		for (size_t i = 0; i < numSupportedResn; i++) {
			edid->suppResolutionList
			    [edid->numOfSupportedResolution] =
			    HdmiSupportedResolution[i];
			edid->numOfSupportedResolution++;
		}
		free(raw);
	} else {
		return dsERR_OPERATION_NOT_SUPPORTED;
	}
	return dsERR_NONE;
}

/**
 * @brief Gets the EDID buffer and EDID length of connected display device.
 *
 * This function is used to get the EDID buffer and EDID size of the connected
 * display corresponding to the specified display device handle.
 *
 * @param[in] handle    - Handle of the display device
 * @param[out] edid     - Pointer to raw EDID buffer
 * @param[out] length   - length of the EDID buffer data. Min value is 0
 *
 * @note Caller is responsible for allocating memory for edid( please refer
 * ::MAX_EDID_BYTES_LEN ) and freeing the EDID buffer
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this
 * API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetEDIDBytes(intptr_t handle, unsigned char *edid, int *length) {
	hal_dbg("invoked.\n");
	uint8_t buffer[128];
	size_t offset = 0;
	int i, extensions = 0;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}
	if (edid == NULL || length == NULL || vDispHandle == NULL ||
	    vDispHandle != &_handles[dsVIDEOPORT_TYPE_HDMI][0]) {
		return dsERR_INVALID_PARAM;
	}
	*length = 0;
	int siz = vc_tv_hdmi_ddc_read(offset, sizeof(buffer), buffer);
	if (siz <= 0) {
		hal_dbg("vc_tv_hdmi_ddc_read failed returning %d.\n", siz);
		return dsERR_GENERAL;
	}
	offset += sizeof(buffer);
	extensions =
	    buffer[0x7e]; /* This tells you how many more blocks to read */
	memcpy(edid, (unsigned char *)buffer, sizeof(buffer));
	/* First block always exist */
	for (i = 0; i < extensions; i++, offset += sizeof(buffer)) {
		siz = vc_tv_hdmi_ddc_read(offset, sizeof(buffer), buffer);
		if (siz <= 0) {
			hal_dbg("vc_tv_hdmi_ddc_read failed returning %d.\n",
			        siz);
		} else {
			memcpy(edid + offset, (unsigned char *)buffer,
			       sizeof(buffer));
		}
	}
	*length = offset;
	return dsERR_NONE;
}

/**
 * @brief Gets the aspect ratio of connected display device.
 *
 * This function returns the aspect ratio of the display corresponding to the
 * specified display device handle.
 *
 * @param[in]  handle       - Handle of the display device
 * @param[out] aspectRatio  - Current aspect ratio of the specified display
 * device Please refer ::dsVideoAspectRatio_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this
 * API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsGetDisplayAspectRatio(intptr_t handle,
                                  dsVideoAspectRatio_t *aspect) {
	hal_dbg("invoked.\n");
	TV_DISPLAY_STATE_T tvstate;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}
	if (vDispHandle == NULL || NULL == aspect) {
		return dsERR_INVALID_PARAM;
	}

	if (vc_tv_get_display_state(&tvstate) == 0) {
		if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
			hal_dbg("HDMI aspect ratio: %d\n",
			        tvstate.display.hdmi.aspect_ratio);
			switch (tvstate.display.hdmi.aspect_ratio) {
				case HDMI_ASPECT_16_9:
					*aspect = dsVIDEO_ASPECT_RATIO_16x9;
					break;
				case HDMI_ASPECT_4_3:
				default:
					*aspect = dsVIDEO_ASPECT_RATIO_4x3;
					break;
			}
		} else if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_BB) {
			hal_dbg("SDTV aspect ratio: %d\n",
			        tvstate.display.sdtv.display_options.aspect);
			switch (tvstate.display.sdtv.display_options.aspect) {
				case SDTV_ASPECT_16_9:
					*aspect = dsVIDEO_ASPECT_RATIO_16x9;
					break;
				case SDTV_ASPECT_4_3:
				default:
					*aspect = dsVIDEO_ASPECT_RATIO_4x3;
					break;
			}
		}
	} else {
		hal_err("vc_tv_get_display_state failed.\n");
		return dsERR_GENERAL;
	}
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
 * @param[in] cb        - Display Event callback function. Please refer
 * ::dsDisplayEventCallback_t
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() and dsGetDisplay() must be called before calling this
 * API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsDisplayEventCallback_t()
 *
 */
dsError_t dsRegisterDisplayEventCallback(intptr_t handle,
                                         dsDisplayEventCallback_t cb) {
	hal_dbg("invoked.\n");
	// VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}
	if (NULL == cb) {
		return dsERR_INVALID_PARAM;
	}
	/* Register The call Back */
	if (NULL != _halcallback) {
		hal_warn(
		    "Callback already registered; override with new one.\n");
	}
	_halcallback = cb;
#ifdef USE_NEW_IMPLEMENTATION
	/* trigger the connection status update once. */
	hdmi_status_change_handler("card0");
#endif /* USE_NEW_IMPLEMENTATION */
	return dsERR_NONE;
}

/**
 * @brief Terminates the display sub-system.
 *
 * This function resets any data structures used within Display sub-system,
 * and releases all the resources allocated during the init function.
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre  dsDisplayInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsDisplayInit()
 *
 */
dsError_t dsDisplayTerm() {
	hal_dbg("invoked.\n");
	dsError_t res = dsERR_NONE;
	if (false == _bDisplayInited) {
		return dsERR_NOT_INITIALIZED;
	}
	vchi_tv_uninit();
#ifdef USE_NEW_IMPLEMENTATION
	hal_dbg("Signaling udevmon thread to exit.\n");
	signal_udevmon_exit();
	pthread_join(thread_udevmon, NULL);
#endif /* USE_NEW_IMPLEMENTATION */
	if (HdmiSupportedResolution) {
		free(HdmiSupportedResolution);
		HdmiSupportedResolution = NULL;
	}
	_bDisplayInited = false;
	return res;
}

static dsVideoPortResolution_t *dsgetResolutionInfo(const char *res_name) {
	size_t iCount = 0;
	iCount = (sizeof(kResolutions) / sizeof(kResolutions[0]));
	for (size_t i = 0; i < iCount; i++) {
		if (!strncmp(res_name, kResolutions[i].name,
		             strlen(res_name))) {
			return &kResolutions[i];
		}
	}
	return NULL;
}

/**
 *	Get The HDMI Resolution List
 **/
static dsError_t dsQueryHdmiResolution() {
	hal_dbg("invoked.\n");
	dsError_t ret = dsERR_NONE;
	static TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_CODE_ID];
	HDMI_RES_GROUP_T group;
	uint32_t mode;
	int num_of_modes;
	memset(modeSupported, 0, sizeof(modeSupported));

	num_of_modes = vc_tv_hdmi_get_supported_modes_new(
	    HDMI_RES_GROUP_CEA, modeSupported, vcos_countof(modeSupported),
	    &group, &mode);
	if (num_of_modes < 0) {
		hal_err("vc_tv_hdmi_get_supported_modes_new failed.\n");
		return ret;
	}
	if (HdmiSupportedResolution) {
		free(HdmiSupportedResolution);
		HdmiSupportedResolution = NULL;
	}
	numSupportedResn = 0;
	size_t iCount = (sizeof(resolutionMap) / sizeof(resolutionMap[0]));
	HdmiSupportedResolution = (dsVideoPortResolution_t *)malloc(
	    sizeof(dsVideoPortResolution_t) * iCount);
	if (HdmiSupportedResolution) {
		for (size_t i = 0; i < iCount; i++) {
			for (int j = 0; j < num_of_modes; j++) {
				hal_dbg(
				    "Resolution from Table '%s' - Mode %d\n",
				    resolutionMap[i].rdkRes,
				    resolutionMap[i].mode);
				hal_dbg("Mode %d:\n", j);
				hal_dbg("  Width: %d\n",
				        modeSupported[j].width);
				hal_dbg("  Height: %d\n",
				        modeSupported[j].height);
				hal_dbg("  Frame Rate: %d\n",
				        modeSupported[j].frame_rate);
				hal_dbg("  Scan Mode: %s\n",
				        modeSupported[j].scan_mode
				            ? "Interlaced"
				            : "Progressive");
				hal_dbg("  Native: %s\n",
				        modeSupported[j].native ? "Yes" : "No");
				hal_dbg("  Aspect Ratio: %d\n",
				        modeSupported[j].aspect_ratio);
				hal_dbg("  Pixel Clock: %d\n",
				        modeSupported[j].pixel_freq);
				hal_dbg("  Pixel Repetition: %d\n",
				        modeSupported[j].pixel_rep);
				hal_dbg("  Group: %d\n",
				        modeSupported[j].group);
				hal_dbg("  Code: %d\n", modeSupported[j].code);
				hal_dbg("  3D Structure Mask: 0x%X\n",
				        modeSupported[j].struct_3d_mask);
				if (modeSupported[j].code ==
				    resolutionMap[i].mode) {
					dsVideoPortResolution_t *resolution =
					    dsgetResolutionInfo(
					        resolutionMap[i].rdkRes);
					memcpy(&HdmiSupportedResolution
					           [numSupportedResn],
					       resolution,
					       sizeof(dsVideoPortResolution_t));
					hal_dbg("Supported Resolution '%s'\n",
					        HdmiSupportedResolution
					            [numSupportedResn]
					                .name);
					numSupportedResn++;
				}
			}
		}
	}
	hal_warn("Total Device supported resolutions on HDMI = %d.\n",
	         numSupportedResn);
	return dsERR_NONE;
}

TV_SUPPORTED_MODE_T dsVideoPortgetVideoFormatFromInfo(dsVideoResolution_t res,
                                                      unsigned frameRate,
                                                      bool interlaced) {
	hal_dbg("invoked.\n");
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
		case dsVIDEO_PIXELRES_1920x1080:
			format.height = 1080;
			break;
		case dsVIDEO_PIXELRES_MAX:  // to mute compiler warning
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
		case dsVIDEO_FRAMERATE_50:
			format.frame_rate = 50;
			break;
		case dsVIDEO_FRAMERATE_60:
			format.frame_rate = 60;
			break;
		default:
			break;
	}
	if (interlaced) {
		format.scan_mode = 1;  // Interlaced
	} else {
		format.scan_mode = 0;  // Progressive
	}
	return format;
}

/************************* Unused/Deprecated Implementations ??
 * ***************************/

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
dsError_t dsDisplaygetNativeHandle(intptr_t handle, int *native) {
	hal_err("invoked.\n");
	dsError_t ret = dsERR_NONE;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *)handle;

	if (vDispHandle == NULL) {
		ret = dsERR_NONE;
	}
	if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI &&
	    vDispHandle->m_index == 0) {
		*native = vDispHandle->m_nativeHandle;
	} else {
		ret = dsERR_NONE;
	}
	return ret;
}
