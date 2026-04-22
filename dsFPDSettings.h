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

#ifndef __DS_FPD_SETTINGS_H__
#define __DS_FPD_SETTINGS_H__

#include "dsTypes.h"

/*
 * Platform-specific Front Panel Display settings for Raspberry Pi 4.
 *
 * RPi4 has a single green ACT LED used as the POWER indicator.
 * No multi-color LED support and no 7-segment display text display.
 */

#define _MAX_BRIGHTNESS     100
#define _MIN_BRIGHTNESS     0
#define _DEFAULT_LEVELS     10
#define _MAX_CYCLERATE      2
#define _MAX_HORZ_COLS      0
#define _MAX_VERT_ROWS      0
#define _MAX_HORZ_ITER      0
#define _MAX_VERT_ITER      0
#define _DEFAULT_COLOR_MODE 0

/*
 * Supported colors for the Power indicator (single green LED).
 */
dsFPDColorConfig_t kFPDIndicatorColors[] = {
    {
        /*.Id = */    0,
        /*.color = */ dsFPD_COLOR_GREEN,
    },
};

/*
 * Front Panel Indicator configurations.
 *
 * Only the POWER indicator is supported on RPi4.
 */
dsFPDIndicatorConfig_t kIndicators[] = {
    {
        /*.id = */              dsFPD_INDICATOR_POWER,
        /*.supportedColors = */ kFPDIndicatorColors,
        /*.maxBrightness   = */ _MAX_BRIGHTNESS,
        /*.maxCycleRate    = */ _MAX_CYCLERATE,
        /*.minBrightness   = */ _MIN_BRIGHTNESS,
        /*.levels          = */ _DEFAULT_LEVELS,
        /*.colorMode       = */ _DEFAULT_COLOR_MODE,
    },
};

/*
 * Front Panel Text Display configurations.
 *
 * RPi4 has no 7-segment text display; this array is empty.
 */
dsFPDTextDisplayConfig_t kFPDTextDisplays[] = {
};

int kFPDTextDisplays_size = 0;

int kFPDIndicatorColors_size = sizeof(kFPDIndicatorColors)/sizeof(kFPDIndicatorColors[0]);

int kIndicators_size = sizeof(kIndicators)/sizeof(kIndicators[0]);

#endif /* __DS_FPD_SETTINGS_H__ */