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

#ifdef DS_HAL_EXPORT_CONFIG_SYMBOLS
extern dsFPDColorConfig_t       kFPDIndicatorColors[];
extern dsFPDIndicatorConfig_t   kIndicators[];
extern dsFPDTextDisplayConfig_t kFPDTextDisplays[];
extern int                      kFPDIndicatorColors_size;
extern int                      kIndicators_size;
extern int                      kFPDTextDisplays_size;
#else
/* Static fallback tables for middleware compile-time dsUTL_DIM checks. */
static dsFPDColorConfig_t kIndicatorColors[] = {
	{ 0, dsFPD_COLOR_GREEN },
};

static dsFPDIndicatorConfig_t kIndicators[] = {
	{
		dsFPD_INDICATOR_POWER,
		kIndicatorColors,
		_MAX_BRIGHTNESS,
		_MAX_CYCLERATE,
		_MIN_BRIGHTNESS,
		_DEFAULT_LEVELS,
		_DEFAULT_COLOR_MODE,
	},
};

/* one inert entry keeps dsUTL_DIM(kTextDisplays) valid in C/C++ */
static dsFPDTextDisplayConfig_t kTextDisplays[] = {
	{ 0, 0, 0, 0 },
};
#endif

#endif /* _DS_FPD_SETTINGS_H_ */
