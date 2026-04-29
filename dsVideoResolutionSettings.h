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
#endif /* __cplusplus */

#define  _INTERLACED true
#define _PROGRESSIVE false

/*
 * Declarations of HAL-exported video resolution table.
 * Definitions reside in dsVideoResolutionSettingsData.c and are exported from the HAL
 * shared library. Use dlsym() / LoadDLSymbols() to obtain runtime pointers from the
 * middleware; never define these symbols in middleware code.
 */
extern dsVideoPortResolution_t kResolutionsSettings[];
extern int                     kResolutionsSettings_size;
extern size_t                  kNumResolutionsSettings;
extern int                     kDefaultResIndex;

/* Alias expected by devicesettings middleware */
#define kResolutions kResolutionsSettings

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _DS_VIDEORESOLUTIONSETTINGS_H_ */
