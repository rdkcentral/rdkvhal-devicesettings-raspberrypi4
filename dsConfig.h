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
#ifndef __DSCONFIG_H
#define __DSCONFIG_H
#include "dsError.h"

typedef dsError_t (*port_allocation_fp)(size_t);
typedef dsError_t (*port_initialization_fp)(size_t index,char* propName,char* Values);
char* getProperty(char* prop,size_t index,char* portType);
char* dsGetValue(char* property);
char* dsGetValidStringFrmCfg(FILE* fptr);
size_t dsGetIndexFrmCfg(char* indexString);
char* dsGetPropertyFrmCfg(char* prop,size_t index,char* portType);
dsError_t dsReadCfgFile(size_t index,char* portString,port_initialization_fp init);

#endif

