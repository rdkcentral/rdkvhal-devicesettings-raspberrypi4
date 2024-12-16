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

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "dsTypes.h"
#include "dsError.h"
#include "dsHost.h"
extern "C" {
#include "interface/vmcs_host/vc_vchi_gencmd.h"
}
static uint32_t version_num = 0x10000;
static bool host_initialized = false;
#define FILE_SIZE 50
#define SIZE 10
dsError_t dsHostInit()
{
    dsError_t ret = dsERR_NONE;
    if (host_initialized) { 
        dsError_t ret = dsERR_ALREADY_INITIALIZED;
        return ret;
    }
    // Initialization code here
    host_initialized = true;
    return ret; 
}

dsError_t dsSetHostPowerMode(int newPower)
{
    dsError_t ret = dsERR_NONE;
    /* Raspberry pi doesn't have anykind of power management It is either plugged in or not.*/
    return ret;
}

dsError_t dsGetHostPowerMode(int *currPower)
{
    dsError_t ret = dsERR_NONE;
    /* Raspberry pi doesn't have anykind of power management It is either plugged in or not.*/
    return ret;
}

dsError_t dsGetCPUTemperature(float *cpuTemperature)
{
    dsError_t ret = dsERR_NONE;
    char Temp_File[FILE_SIZE];
    char temp_value[SIZE];
    FILE *fp = NULL;
    if (!host_initialized) {                                                                                                                                               
    dsError_t ret = dsERR_NOT_INITIALIZED;                                                                                                                          
    return ret;                                                                                                                                                     
    }                                                                                                                                                                
    if (cpuTemperature == NULL) {                                                                                                                                   
    dsError_t ret = dsERR_INVALID_PARAM;                                                                                                                         
    return ret;                                                                                                                                                  
}   

    snprintf(Temp_File, FILE_SIZE, "/sys/class/thermal/thermal_zone0/temp");
    fp = fopen(Temp_File, "r");
    if (fread(temp_value, 1, SIZE, fp) <= 0) {
        printf("Error reading cpu temp\n");
        return ret;
    }
    *cpuTemperature = atof(temp_value)/1000;
    printf("Cpu temperature %f", *cpuTemperature);
    return ret;
}

dsError_t dsGetVersion(uint32_t *versionNumber)
{
    printf("Entering into dsGetVersion\n");
        dsError_t ret;

        if(versionNumber != NULL)
        {
                printf("getting hal version in ds-hal\n");
                *versionNumber = version_num;
                return dsERR_NONE;
        }
        else ret = dsERR_INVALID_PARAM;
        return ret;
}

dsError_t dsSetVersion(uint32_t versionNumber)
{
   printf("Entering into dsGetVersion\n");
        dsError_t ret = dsERR_NONE;

   printf("setting hal version in ds-hal %x\n", versionNumber);
   version_num = versionNumber;

   return ret;
}

dsError_t dsGetFreeSystemGraphicsMemory(uint64_t* memory)
{
    char buffer[512];
    dsError_t ret = dsERR_NONE;

    buffer[0] = '\0';
    printf("Entering into dsGetFreeSystemGraphicsMemory\n");

    if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc") != 0 )
    {
        printf( "Failed to get free GPU memory\n");
        return dsERR_GENERAL;
    }

    buffer[sizeof(buffer) - 1] = '\0';
    /* Extract response after = */
    char* equal = strchr(buffer, '=');
    if (equal != nullptr) {
        equal++;
    }
    else {
        equal = buffer;
    }

    *memory = strtol(equal, (char **)NULL, 10);

    printf( "Free GPU memory is %lld\n", *memory);

    return ret;

}

dsError_t dsGetTotalSystemGraphicsMemory(uint64_t* memory)
{
    char buffer[512];
    dsError_t ret = dsERR_NONE;

    buffer[0] = '\0';
    printf("Entering into dsGetTotalSystemGraphicsMemory\n");

    if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc_total") != 0 )
    {
        printf( "Failed to get total GPU memory\n");
        return dsERR_GENERAL;
    }

    buffer[sizeof(buffer) - 1] = '\0';
    /* Extract response after = */
    char* equal = strchr(buffer, '=');
    if (equal != nullptr) {
        equal++;
    }
    else {
        equal = buffer;
    }

    *memory = strtol(equal, (char **)NULL, 10);

    printf( "Total GPU memory is %lld\n", *memory);
    return ret;
}

dsError_t dsHostTerm()
{
    dsError_t ret = dsERR_NONE;
    if (!host_initialized) { 
        dsError_t ret = dsERR_NOT_INITIALIZED;
        return ret;
    } 
    host_initialized = false; 
    return ret;
}

dsError_t dsGetHostEDID(unsigned char *edid, int *length) {
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetSocIDFromSDK(char *socID)
{
    if (!host_initialized) {                                                                                                                                               
    return dsERR_NOT_INITIALIZED;                                                                                                                                  
    }  
    if (socID == NULL)
    {
      return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
