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

#ifdef __cplusplus
extern "C" {
namespace {
#endif


#define  _INTERLACED true
#define _PROGRESSIVE false

#define dsVideoPortRESOLUTION_NUMMAX 32

/* List all supported resolutions here */
typedef struct __hdmiSupportedRes_t {
    const char *rdkRes;
    int mode;
}hdmiSupportedRes_t;

static dsVideoPortResolution_t kResolutions[] = {
                {                                            "480p",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_720x480,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_60,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "576p50",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_720x576,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_50,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "720p",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1280x720,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_60,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "720p50",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1280x720,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_50,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "1080i",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_60,
                        /*.interlaced = */                              _INTERLACED,
                },

                {                                            "1080p",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_25,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "1080i50",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_50,
                        /*.interlaced = */                              _INTERLACED,
                },

                {                                            "1080p50",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_50,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "1080p24",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_24,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "1080p30",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_30,
                        /*.interlaced = */                              _PROGRESSIVE,
                },

                {                                            "1080p60",
                        /*.pixelResolution = */                 dsVIDEO_PIXELRES_1920x1080,
                        /*.aspectRatio = */                             dsVIDEO_ASPECT_RATIO_16x9,
                        /*.stereoscopicMode = */                dsVIDEO_SSMODE_2D,
                        /*.frameRate = */                               dsVIDEO_FRAMERATE_60,
                        /*.interlaced = */                              _PROGRESSIVE,
                }
};

static hdmiSupportedRes_t resolutionMap[] = {
                {"480p", 3},
                {"576p50", 18},
                {"720p", 4},
                {"720p50", 19},
                {"1080i", 5},
                {"1080p", 33},
                {"1080i50", 20},
                {"1080p50", 31},
                {"1080p24", 32},
                {"1080p30", 34},
                {"1080p60", 16}
};
static const int kDefaultResIndex = 2; //Pick one resolution from kResolutions[] as default

#ifdef __cplusplus
}
}
#endif

#endif /* VIDEORESOLUTIONSETTINGS_H_ */
