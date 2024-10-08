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
#include "dshalUtils.h"

#define MAX_HDMI_CODE_ID (127)
dsDisplayEventCallback_t _halcallback = NULL;
dsVideoPortResolution_t *HdmiSupportedResolution=NULL;
static unsigned int numSupportedResn = 0;

static bool isBootup = true;
static dsError_t dsQueryHdmiResolution();
TV_SUPPORTED_MODE_T dsVideoPortgetVideoFormatFromInfo(dsVideoResolution_t res,
                                                       unsigned frameRate, bool interlaced);
static dsVideoPortResolution_t* dsgetResolutionInfo(const char *res_name);

typedef struct _VDISPHandle_t {
	dsVideoPortType_t m_vType;
	int m_index;
	int m_nativeHandle;
} VDISPHandle_t;

static VDISPHandle_t _handles[dsVIDEOPORT_TYPE_MAX][2] = {
};


static void tvservice_callback( void *callback_data,
                                uint32_t reason,
                                uint32_t param1,
                                uint32_t param2 )
{
    VDISPHandle_t *hdmiHandle = (VDISPHandle_t*)callback_data;
    unsigned char  eventData=0;
   switch ( reason )
   {
      case VC_HDMI_UNPLUGGED:
      {
         printf( "HDMI cable is unplugged" );
         _halcallback((int)(hdmiHandle->m_nativeHandle),dsDISPLAY_EVENT_DISCONNECTED,&eventData);
         break;
      }
      case VC_HDMI_ATTACHED:
      {
         printf( "HDMI is attached" );
         _halcallback((int)(hdmiHandle->m_nativeHandle),dsDISPLAY_EVENT_CONNECTED,&eventData);
         break;
      }
      default:
      {
         if(isBootup == true)
         {
             printf( "For Rpi - HDMI is attached by default" );
             _halcallback((int)(hdmiHandle->m_nativeHandle),dsDISPLAY_EVENT_CONNECTED,&eventData);
             isBootup = false;
         }
         break;
      }
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

	dsError_t ret = dsERR_NONE;
        int32_t res = 0;

	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_vType  = dsVIDEOPORT_TYPE_HDMI;
	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_nativeHandle = dsVIDEOPORT_TYPE_HDMI;
	_handles[dsVIDEOPORT_TYPE_HDMI][0].m_index = 0;

	_handles[dsVIDEOPORT_TYPE_COMPONENT][0].m_vType  = dsVIDEOPORT_TYPE_BB;
	_handles[dsVIDEOPORT_TYPE_COMPONENT][0].m_nativeHandle = dsVIDEOPORT_TYPE_BB;
	_handles[dsVIDEOPORT_TYPE_COMPONENT][0].m_index = 0;
    res = vchi_tv_init();
    if (res != 0) {
        printf("Unable to initialise tv servic\n");
        return dsERR_GENERAL;
    }
    // Register callback for HDMI hotplug
    vc_tv_register_callback( &tvservice_callback, &_handles[dsVIDEOPORT_TYPE_HDMI][0] );
	/*Query the HDMI Resolution */
    dsQueryHdmiResolution();

    return ret;
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
    dsError_t ret = dsERR_NONE;


    if (index != 0 || !dsVideoPortType_isValid(m_vType)) {
        ret = dsERR_NONE;
    }

    if (ret == dsERR_NONE) {
        *handle = (intptr_t)&_handles[m_vType][index];

    } else {
    }

    return ret;

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
	dsError_t ret = dsERR_NONE;
        TV_DISPLAY_STATE_T tvstate;
        VDISPHandle_t *vDispHandle = (VDISPHandle_t *) handle;
	if (vDispHandle == NULL)
	{
		printf("DIsplay Handle is NULL .......... \r\n");
		return ret;
	}
        	
        if( vc_tv_get_display_state( &tvstate ) == 0) {
            if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
                switch(tvstate.display.hdmi.aspect_ratio) {
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
                switch(tvstate.display.sdtv.display_options.aspect) {
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
        }
        else {
           printf("Error getting current display state");
        }
    return ret;
}



/**
 * @brief Register for a callback routine for HDMI Hotplug
 *
 * This function is used to register for a call back for HDMI connection and disconnect events
 *
 * @param [in] handle   Handle for the video display
 * @param [out] *edid   The callback rouutine
 * @return dsError_t Error code.
 */

dsError_t dsRegisterDisplayEventCallback(intptr_t handle, dsDisplayEventCallback_t cb)
{
	dsError_t ret = dsERR_NONE;
	/* Register The call Back */
	_halcallback = cb;
	return ret;
}

/**
 * @brief To get the EDID information of the connected display
 *
 * This function is used to get the EDID information of the connected display
 *
 * @param [in] handle   Handle for the video display
 * @param [out] *edid   The EDID info of the display
 * @return dsError_t Error code.
 */
dsError_t dsGetEDID(intptr_t handle, dsDisplayEDID_t *edid)
{
	dsError_t ret = dsERR_NONE;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *) handle;

	if (vDispHandle == NULL)
	{
		ret = dsERR_NONE;
		printf("DIsplay Handle is NULL .......... \r\n");
		return ret;
	}
        unsigned char *raw = NULL;
        int length = 0;
        edid->numOfSupportedResolution = 0;
        if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
            dsGetEDIDBytes(handle, raw, &length);
            fill_edid_struct(raw, edid, length);
            dsQueryHdmiResolution();

            printf("numSupportedResn - %d .......... \r\n",numSupportedResn);
            for (size_t i = 0; i < numSupportedResn; i++)
            {
                edid->suppResolutionList[edid->numOfSupportedResolution] = HdmiSupportedResolution[i];
                edid->numOfSupportedResolution++;
            }
            free(raw);
                
        } else {
               ret = dsERR_OPERATION_NOT_SUPPORTED;
        }
	return ret;
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
    dsError_t res = dsERR_NONE;
    vchi_tv_uninit();
    if(HdmiSupportedResolution)
    {
        free(HdmiSupportedResolution);
        HdmiSupportedResolution=NULL;
    }    
    return res;
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
	dsError_t ret = dsERR_NONE;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *) handle;

	if (vDispHandle == NULL)
	{
		ret = dsERR_NONE;
	}
	if (vDispHandle->m_vType == dsVIDEOPORT_TYPE_HDMI && vDispHandle->m_index == 0) {
		*native = vDispHandle->m_nativeHandle;
    }
	else {
		ret = dsERR_NONE;
	}

    return ret;

}


/**
 *	Get The HDMI Resolution L:ist
 *
 **/
static dsError_t dsQueryHdmiResolution()
{

    dsError_t ret = dsERR_NONE;
   static TV_SUPPORTED_MODE_NEW_T modeSupported[MAX_HDMI_CODE_ID];
   HDMI_RES_GROUP_T group;
   uint32_t mode;
   int num_of_modes;
   memset(modeSupported, 0, sizeof(modeSupported));

   num_of_modes = vc_tv_hdmi_get_supported_modes_new( HDMI_RES_GROUP_CEA, modeSupported,
                                               vcos_countof(modeSupported),
                                               &group,
                                               &mode );
   if ( num_of_modes < 0 )
   {
      printf( "Failed to get modes" );
      return ret;
   }
    if(HdmiSupportedResolution)
    {
                  free(HdmiSupportedResolution);
                  HdmiSupportedResolution=NULL;
    }
    numSupportedResn = 0;
    size_t iCount = (sizeof(resolutionMap) / sizeof(resolutionMap[0]));
    HdmiSupportedResolution=(dsVideoPortResolution_t*)malloc(sizeof(dsVideoPortResolution_t)*iCount);
    if(HdmiSupportedResolution)
    {
		for (size_t i = 0; i < iCount; i++)
		{
                        for ( int j = 0; j < num_of_modes; j++ )
                        {
                            if (modeSupported[j].code == resolutionMap[i].mode)
                            {
                                dsVideoPortResolution_t *resolution = dsgetResolutionInfo(resolutionMap[i].rdkRes);
                                memcpy(&HdmiSupportedResolution[numSupportedResn], resolution, sizeof(dsVideoPortResolution_t));
				printf("Supported Resolution %s \r\n",HdmiSupportedResolution[numSupportedResn].name);
				numSupportedResn++;
                            }
                        }
		}
	}
	printf("%s: Total Device supported resolutions on HDMI = %d \r\n",__FUNCTION__, numSupportedResn);


return dsERR_NONE;
}

static dsVideoPortResolution_t* dsgetResolutionInfo(const char *res_name)
{
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
    TV_SUPPORTED_MODE_T format = {0};
    switch(res) {
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
        case dsVIDEO_PIXELRES_MAX: //to mute compiler warning
        default:
                break;
        }
      
    switch(frameRate) {
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
	dsError_t ret = dsERR_NONE;
	uint8_t buffer[128];
	size_t offset = 0;
	int i, extensions = 0;
	VDISPHandle_t *vDispHandle = (VDISPHandle_t *) handle;
	*length = 0;

	if (edid == NULL || length == NULL) {
		printf("[%s] invalid params\n", __FUNCTION__);
		return dsERR_INVALID_PARAM;
	}
	else if (vDispHandle == NULL || vDispHandle != &_handles[dsVIDEOPORT_TYPE_HDMI][0])
	{
		printf("[%s] invalid handle\n", __FUNCTION__);
		return dsERR_INVALID_PARAM;
	}
	int siz = vc_tv_hdmi_ddc_read(offset, sizeof (buffer), buffer);
	offset += sizeof( buffer);
	extensions = buffer[0x7e]; /* This tells you how many more blocks to read */
	memcpy(edid, (unsigned char *)buffer, sizeof(buffer));
	/* First block always exist */
	for(i = 0; i < extensions; i++, offset += sizeof( buffer)) {
		siz = vc_tv_hdmi_ddc_read(offset, sizeof( buffer), buffer);
		memcpy(edid+offset, (unsigned char *)buffer, sizeof(buffer));
	}
	*length = offset;
    return ret;
}

