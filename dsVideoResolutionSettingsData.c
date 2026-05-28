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
 *
 * SPDX-License-Identifier: Apache-2.0
*/

/**
 * @file dsVideoResolutionSettingsData.c
 * @brief Definitions of the HAL-exported video resolution table.
 *
 * Resolved at runtime by the middleware via dlsym() / LoadDLSymbols().
 */

#include "dsTypes.h"
#include "dsVideoResolutionSettings.h"

/* Exported resolution table - looked up via dlsym() by the middleware
 *
 * IMPORTANT ORDERING: Implicit-rate entries (bare tokens like "480p", "720p", "1080p", "1080i")
 * MUST come before their explicit-rate counterparts ("480p60", "720p60", "1080p60", "1080i60")
 * to ensure dsgetResolutionInfo() prefix-match fallback returns the correct default rate.
 * If adding or removing entries, maintain this grouping:
 *   - "480p" before "480p60"
 *   - "480i" before "480i60"
 *   - "576i" before "576i50"
 *   - "576p" before "576p50"
 *   - "720p" before "720p60" / "720p50"
 *   - "1080p" before "1080p24"/"1080p25"/"1080p30"/"1080p50"/"1080p60"
 *   - "1080i" before "1080i60" / "1080i50"
 *   - "2160p" before "2160p24"/"2160p25"/"2160p30"/"2160p50"/"2160p60"
 */
dsVideoPortResolution_t kResolutionsSettings[] = {
    {   /*480p*/
        /*.name = */                "480p",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*480p60*/
        /*.name = */                "480p60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*480i*/
        /*.name = */                "480i",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*480i60*/
        /*.name = */                "480i60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x480,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*576i*/
        /*.name = */                "576i",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*576i50*/
        /*.name = */                "576i50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*576p*/
        /*.name = */                "576p",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*576p50*/
        /*.name = */                "576p50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_720x576,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*720p*/
        /*.name = */                "720p",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1280x720,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*720p60*/
        /*.name = */                "720p60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1280x720,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*720p50*/
        /*.name = */                "720p50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1280x720,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p*/
        /*.name = */                "1080p",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p24*/
        /*.name = */                "1080p24",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_24,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p25*/
        /*.name = */                "1080p25",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_25,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p30*/
        /*.name = */                "1080p30",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_30,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p50*/
        /*.name = */                "1080p50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080p60*/
        /*.name = */                "1080p60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*1080i*/
        /*.name = */                "1080i",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*1080i60*/
        /*.name = */                "1080i60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*1080i50*/
        /*.name = */                "1080i50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_1920x1080,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _INTERLACED,
    },
    {   /*2160p*/
        /*.name = */                "2160p",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*2160p24*/
        /*.name = */                "2160p24",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_24,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*2160p25*/
        /*.name = */                "2160p25",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_25,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*2160p30*/
        /*.name = */                "2160p30",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_30,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*2160p50*/
        /*.name = */                "2160p50",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_50,
        /*.interlaced = */          _PROGRESSIVE,
    },
    {   /*2160p60*/
        /*.name = */                "2160p60",
        /*.pixelResolution = */     dsVIDEO_PIXELRES_3840x2160,
        /*.aspectRatio = */         dsVIDEO_ASPECT_RATIO_16x9,
        /*.stereoscopicMode = */    dsVIDEO_SSMODE_2D,
        /*.frameRate = */           dsVIDEO_FRAMERATE_60,
        /*.interlaced = */          _PROGRESSIVE,
    },
};

int    kResolutionsSettings_size = sizeof(kResolutionsSettings) / sizeof(kResolutionsSettings[0]);
size_t kNumResolutionsSettings   = sizeof(kResolutionsSettings) / sizeof(kResolutionsSettings[0]);

/* Default resolution index: 720p (index 8) matches RPi boot configuration. */
int kDefaultResIndex = 8;
