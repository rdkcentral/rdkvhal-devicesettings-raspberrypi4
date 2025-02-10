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
#include "dshalLogger.h"
#include "interface/vmcs_host/vc_vchi_gencmd.h"

static uint32_t version_num = 0x10000;
static bool host_initialized = false;

#define BUFFER_SIZE 512

#define SYS_CPU_TEMP "/sys/class/thermal/thermal_zone0/temp"
#define PROC_CPUINFO "/proc/cpuinfo"

/**
 * @brief Initializes the Host HAL sub-system
 *
 * This function initializes any needed resources within the module.
 *
 * @return dsError_t                    - Status
 * @retval dsERR_NONE                   - Success
 * @retval dsERR_ALREADY_INITIALIZED    - Function is already initialized
 * @retval dsERR_GENERAL                - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 * @see dsHostTerm()
 *
 */
dsError_t dsHostInit()
{
    hal_info("invoked.\n");
    if (host_initialized) {
        return dsERR_ALREADY_INITIALIZED;
    }
    // Initialization code here
    host_initialized = true;
    return dsERR_NONE;
}

/**
 * @brief Terminates the Host sub-system
 *
 * This function has to release all the resources allocated in the
 * initialisation function.
 *
 * @return dsError_t                - Status
 * @retval dsERR_NONE               - Success
 * @retval dsERR_NOT_INITIALIZED    - Module is not initialised
 * @retval dsERR_GENERAL            - General failure
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsHostInit()
 *
 */
dsError_t dsHostTerm()
{
    hal_info("invoked.\n");
    if (!host_initialized) {
        return dsERR_NOT_INITIALIZED;
    }
    host_initialized = false;
    return dsERR_NONE;
}

/**
 * @brief Gets the CPU temperature in centigrade
 *
 * @param[out] cpuTemperature   - CPU temperature value returned in centigrade
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre dsHostInit() must be called before this function
 *
 * @warning  This API is Not thread safe.
 *
 */
dsError_t dsGetCPUTemperature(float *cpuTemperature)
{
    hal_info("invoked.\n");

    if (!host_initialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (cpuTemperature == NULL) {
        hal_err("Invalid parameter, cpuTemperature(%p)\n", cpuTemperature);
        return dsERR_INVALID_PARAM;
    }

    FILE *fp = fopen(SYS_CPU_TEMP, "r");
    if (fp == NULL) {
        hal_err("Error opening cpu temp file '%s'\n", SYS_CPU_TEMP);
        return dsERR_GENERAL;
    }

    char temp_value[BUFFER_SIZE] = {0};
    int len = fread(temp_value, 1, BUFFER_SIZE - 1, fp);
    fclose(fp);

    if (len == 0) {
        hal_err("Error reading cpu temp value from '%s'\n", SYS_CPU_TEMP);
        return dsERR_GENERAL;
    }

    temp_value[len] = '\0';
    *cpuTemperature = atof(temp_value) / 1000;

    if (*cpuTemperature == 0 && temp_value[0] != '0') {
        hal_err("Error converting cpu temp value '%s'\n", temp_value);
        return dsERR_GENERAL;
    }

    hal_dbg("CPU temperature is %f\n", *cpuTemperature);
    return dsERR_NONE;
}

/**
 * @brief Returns the SOC ID
 *
 * @param[out] socID    - 8 byte Chip ID programmed to the CHIP One Time
 * Programmable area
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre dsHostInit() must be called before this function
 *
 * @warning  This API is Not thread safe.
 *
 */
dsError_t dsGetSocIDFromSDK(char *socID)
{
    hal_info("invoked.\n");

    if (!host_initialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (socID == NULL) {
        hal_err("Invalid parameter, socID(%p)\n", socID);
        return dsERR_INVALID_PARAM;
    }

    FILE *fp = fopen(PROC_CPUINFO, "r");
    if (fp == NULL) {
        hal_err("Error opening cpuinfo file '%s'\n", PROC_CPUINFO);
        return dsERR_GENERAL;
    }

    char line[BUFFER_SIZE] = {0};
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strstr(line, "Hardware") != NULL) {
            char *value = strchr(line, ':');
            if (value != NULL) {
                value++;
                while (*value == ' ') {
                    value++;
                }
                strncpy(socID, value, 7);
                socID[7] = '\0';
                break;
            }
        }
    }
    fclose(fp);

    if (strlen(socID) == 0) {
        hal_err("Error reading socID from '%s'\n", PROC_CPUINFO);
        return dsERR_GENERAL;
    }

    hal_dbg("SOC ID is %s\n", socID);
    return dsERR_NONE;
}

/**
 * @brief Gets the host EDID and length
 *
 * The host EDID will be used on devices supporting HDMI input feature.
 *
 * @param[out] edid     - host EDID.
 * @param[out] length   - length of host EDID. Min value of 0.  Max value of
 * 2048
 *
 * @return dsError_t                        - Status
 * @retval dsERR_NONE                       - Success
 * @retval dsERR_NOT_INITIALIZED            - Module is not initialised
 * @retval dsERR_INVALID_PARAM              - Parameter passed to this function
 * is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not
 * supported
 * @retval dsERR_GENERAL                    - Underlying undefined platform
 * error
 *
 * @pre dsHostInit() must be called before this function
 *
 * @warning  This API is Not thread safe.
 *
 */
dsError_t dsGetHostEDID(unsigned char *edid, int *length)
{
    hal_info("invoked.\n");
    if (!host_initialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (edid == NULL || length == NULL) {
		hal_err("Invalid parameter, edid(%p), length(%p)\n", edid, length);
        return dsERR_INVALID_PARAM;
    }
	// RPi does not have HDMI-In feature
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetHostPowerMode(int newPower)
{
    hal_warn("invoked; deprecated ?.\n");
    if (newPower < dsPOWER_ON || newPower >= dsPOWER_MAX) {
		hal_err("Invalid power mode %d\n", newPower);
		return dsERR_INVALID_PARAM;
	}
    /* Raspberry pi doesn't have anykind of power management It is either
     * plugged in or not.*/
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHostPowerMode(int *currPower)
{
    hal_warn("invoked; deprecated ?.\n");
    if (currPower == NULL) {
		hal_err("Invalid parameter, currPower(%p)\n", currPower);
		return dsERR_INVALID_PARAM;
	}
    /* Raspberry pi doesn't have anykind of power management It is either
     * plugged in or not.*/
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetVersion(uint32_t *versionNumber)
{
    hal_warn("invoked; deprecated ?.\n");

    if (versionNumber != NULL) {
        hal_dbg("Getting hal version in ds-hal 0x%x\n", version_num);
        *versionNumber = version_num;
        return dsERR_NONE;
    }
    return dsERR_INVALID_PARAM;
}

dsError_t dsSetVersion(uint32_t versionNumber)
{
    hal_warn("invoked; deprecated ?.\n");
    version_num = versionNumber;
    hal_dbg("Setting hal version in ds-hal 0x%x\n", version_num);
    return dsERR_NONE;
}

dsError_t dsGetFreeSystemGraphicsMemory(uint64_t *memory)
{
    hal_warn("invoked; deprecated ?.\n");
	if (memory == NULL) {
		hal_err("Invalid parameter, memory(%p)\n", memory);
		return dsERR_INVALID_PARAM;
	}
    char buffer[BUFFER_SIZE] = {0};

    if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc") != 0) {
        hal_err("Failed to get free GPU memory\n");
        return dsERR_GENERAL;
    }

    buffer[sizeof(buffer) - 1] = '\0';
    /* Extract response after = */
    char *equal = strchr(buffer, '=');
    if (equal != NULL) {
        equal++;
    } else {
        equal = buffer;
    }

    *memory = strtol(equal, (char **)NULL, 10);
    hal_dbg("Free GPU memory is %lld\n", *memory);

    return dsERR_NONE;
}

dsError_t dsGetTotalSystemGraphicsMemory(uint64_t *memory)
{
    hal_warn("invoked; deprecated ?.\n");
	if (memory == NULL) {
		hal_err("Invalid parameter, memory(%p)\n", memory);
		return dsERR_INVALID_PARAM;
	}
    char buffer[BUFFER_SIZE] = {0};

    if (vc_gencmd(buffer, sizeof(buffer), "get_mem reloc_total") != 0) {
        hal_err("Failed to get total GPU memory\n");
        return dsERR_GENERAL;
    }

    buffer[sizeof(buffer) - 1] = '\0';
    /* Extract response after = */
    char *equal = strchr(buffer, '=');
    if (equal != NULL) {
        equal++;
    } else {
        equal = buffer;
    }

    *memory = strtol(equal, (char **)NULL, 10);
    hal_dbg("Total GPU memory is %lld\n", *memory);

    return dsERR_NONE;
}
