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

#ifndef _DS_FPD_SETTINGS_H_
#define _DS_FPD_SETTINGS_H_

#include "dsTypes.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

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
 * Declarations of HAL-exported front panel configuration tables.
 * Definitions reside in dsFPDSettingsData.c and are exported from the HAL shared
 * library. Use dlsym() / LoadDLSymbols() to obtain runtime pointers from the
 * middleware; never define these symbols in middleware code.
 */
extern dsFPDColorConfig_t       kFPDIndicatorColors[];
extern dsFPDIndicatorConfig_t   kIndicators[];
extern dsFPDTextDisplayConfig_t kFPDTextDisplays[];
extern int                      kFPDIndicatorColors_size;
extern int                      kIndicators_size;
extern int                      kFPDTextDisplays_size;

/* Aliases expected by devicesettings middleware static fallback */
#define kIndicatorColors kFPDIndicatorColors
#define kTextDisplays    kFPDTextDisplays

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _DS_FPD_SETTINGS_H_ */
