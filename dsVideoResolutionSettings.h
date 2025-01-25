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

#ifndef _DS_VIDEORESOLUTIONSETTINGS_H_
#define _DS_VIDEORESOLUTIONSETTINGS_H_

#include "dsTypes.h"

#define  _INTERLACED true
#define _PROGRESSIVE false

#define dsVideoPortRESOLUTION_NUMMAX 32

/* List all supported resolutions here */
typedef struct __hdmiSupportedRes_t {
    const char *rdkRes;
    int mode;
}hdmiSupportedRes_t;

/******************************************************************************************************
 *
 * root@raspberrypi4-64-rdke:~# tvservice -j -m CEA
 [
 { "code":3,   "width":720,  "height":480,  "rate":60, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":4,   "width":1280, "height":720,  "rate":60, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":5,   "width":1920, "height":1080, "rate":60, "aspect_ratio":"16:9", "scan":"i", "3d_modes":[] },
 { "code":7,   "width":720,  "height":480,  "rate":60, "aspect_ratio":"16:9", "scan":"i", "3d_modes":[] },
 { "code":16,  "width":1920, "height":1080, "rate":60, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":18,  "width":720,  "height":576,  "rate":50, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":19,  "width":1280, "height":720,  "rate":50, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":20,  "width":1920, "height":1080, "rate":50, "aspect_ratio":"16:9", "scan":"i", "3d_modes":[] },
 { "code":22,  "width":720,  "height":576,  "rate":50, "aspect_ratio":"16:9", "scan":"i", "3d_modes":[] },
 { "code":31,  "width":1920, "height":1080, "rate":50, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":32,  "width":1920, "height":1080, "rate":24, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":33,  "width":1920, "height":1080, "rate":25, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":34,  "width":1920, "height":1080, "rate":30, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":93,  "width":3840, "height":2160, "rate":24, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":94,  "width":3840, "height":2160, "rate":25, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":95,  "width":3840, "height":2160, "rate":30, "aspect_ratio":"16:9", "scan":"p", "3d_modes":[] },
 { "code":98,  "width":4096, "height":2160, "rate":24, "aspect_ratio":"unknown AR", "scan":"p", "3d_modes":[] },
 { "code":99,  "width":4096, "height":2160, "rate":25, "aspect_ratio":"unknown AR", "scan":"p", "3d_modes":[] },
 { "code":100, "width":4096, "height":2160, "rate":30, "aspect_ratio":"unknown AR", "scan":"p", "3d_modes":[] }
 ]
 **/

static dsVideoPortResolution_t kResolutions[] = {
    {   /*480p*/
        /*.name = */					"480p",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*480i*/
        /*.name = */					"480i",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_INTERLACED,
    },
    {   /*576i*/
        /*.name = */					"576i",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_50,
        /*.interlaced = */				_INTERLACED,
    },
    {   /*576p*/
        /*.name = */					"576p",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_50,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*720p - Default - AutoSelect */
        /*.name = */					"720p",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1280x720,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*720p - Default - AutoSelect */
        /*.name = */					"720p50",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1280x720,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_50,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*1080p24*/
        /*.name = */                    "1080p24",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_24,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {   /*1080p25*/
        /*.name = */                    "1080p25",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_25,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {   /*1080p30*/
        /*.name = */					"1080p30",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_30,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {       /*1080p50*/
        /*.name = */                    "1080p50",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_50,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {   /*1080p60*/
        /*.name = */					"1080p60",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*1080i*/
        /*.name = */					"1080i50",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_50,
        /*.interlaced = */				_INTERLACED,
    },
    {   /*1080i60*/
        /*.name = */					"1080i",
        /*.pixelResolution = */			dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */				dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_INTERLACED,
    },
    {   /*2160p24*/
        /*.name = */                    "2160p24",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_24,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {   /*2160p25*/
        /*.name = */                    "2160p25",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_25,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {	/*2160p30*/
        /*.name = */					"2160p30",
        /*.pixelResolution = */ 		dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */ 			dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_30,
        /*.interlaced = */				_PROGRESSIVE,
    },
    {   /*2160p50*/
        /*.name = */                    "2160p50",
        /*.pixelResolution = */         dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */             dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */        dsVIDEO_SSMODE_2D,
        /*.frameRate = */               dsVIDEO_FRAMERATE_50,
        /*.interlaced = */              _PROGRESSIVE,
    },
    {	/*2160p60*/
        /*.name = */					"2160p60",
        /*.pixelResolution = */ 		dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */ 			dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */		dsVIDEO_SSMODE_2D,
        /*.frameRate = */				dsVIDEO_FRAMERATE_60,
        /*.interlaced = */				_PROGRESSIVE,
    },
};

static hdmiSupportedRes_t resolutionMap[] = {
    {"480p", 3},
    {"480i", 7},
    {"576i", 22},
    {"576p", 18},
    {"720p", 4},
    {"720p50", 19},
    {"1080p24", 32},
    {"1080p25", 33},
    {"1080p30", 34},
    {"1080p50", 31},
    {"1080p60", 16},
    {"1080i50", 20},
    {"1080i", 5},
    {"2160p24", 93},
    {"2160p25", 94},
    {"2160p30", 95},
    {"2160p50", 99},
    {"2160p60", 100}
};

/* Pick one resolution from kResolutions[] as default - 720p */
static const int kDefaultResIndex = 4;

#endif /* VIDEORESOLUTIONSETTINGS_H_ */
