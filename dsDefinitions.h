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
#ifndef _DS_DISPLAY_H_
#define _DS_DISPLAY_H_

#define INVALID_HANDLE 0
#define ISNULL(ptr) (ptr==NULL)
#define _RETURN_IF_ERROR(cond, err) do {\
    				if (!(cond)) { \
        			 printf("ERROR at [%s][%d]\r\n", __FILE__, __LINE__);\
        			 return (err); \
    				 } \
				}while(0);

typedef struct _dsDisplayParam_t {
        dsDisplayEDID_t edidValue;
        dsVideoAspectRatio_t aspectRatio;
        dsVideoPortPortId_t portId;
} dsDisplayParam_t;

dsDisplayParam_t *dsVideoDisplayHandler = '\0';
dsError_t dsDisplayHdlCheck(intptr_t handle);
size_t videoDisplayMaxIndex = 0;

#endif
