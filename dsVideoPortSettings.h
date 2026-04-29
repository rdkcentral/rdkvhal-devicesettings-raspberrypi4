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

#ifndef _DS_VIDEOOUTPUTPORTSETTINGS_H_
#define _DS_VIDEOOUTPUTPORTSETTINGS_H_

#include "dsTypes.h"
#include "dsUtl.h"
#include "dsVideoResolutionSettings.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Declarations of HAL-exported video port configuration tables.
 * Definitions reside in dsVideoPortSettingsData.c and are exported from the HAL shared
 * library. Use dlsym() / LoadDLSymbols() to obtain runtime pointers from the middleware;
 * never define these symbols in middleware code to avoid multiple-definition issues.
 */
extern dsVideoPortTypeConfig_t  kVideoPortConfigs[];
extern dsVideoPortPortConfig_t  kVideoPortPorts[];
extern int                      kVideoPortConfigs_size;
extern int                      kVideoPortPorts_size;

/* Aliases expected by devicesettings middleware */
#define kConfigs kVideoPortConfigs
#define kPorts   kVideoPortPorts

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _DS_VIDEOOUTPUTPORTSETTINGS_H_ */
