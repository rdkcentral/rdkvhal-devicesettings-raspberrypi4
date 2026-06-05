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

#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>

#include "dsTypes.h"
#include "dsError.h"
#include "dsHost.h"
#include "dshalLogger.h"
#include "dshalUtils.h"

static uint32_t version_num = 0x10000;
static bool host_initialized = false;

#define BUFFER_SIZE 512

static bool soc_id_cached = false;
static char cached_soc_id[BUFFER_SIZE] = {0};

#define SYS_CPU_TEMP "/sys/class/thermal/thermal_zone0/temp"
#define PROC_CPUINFO "/proc/cpuinfo"
#define SYS_DT_COMPATIBLE "/sys/firmware/devicetree/base/compatible"
#define PROC_DT_COMPATIBLE "/proc/device-tree/compatible"
#define SOC_COMPATIBLE_PREFIX "brcm,bcm"

static size_t dsBoundedStrLen(const char *s, size_t max_len)
{
    const char *end = memchr(s, '\0', max_len);
    return (end != NULL) ? (size_t)(end - s) : max_len;
}

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
 * @param[out] socID    - SoC model identifier derived from the device tree
 * compatible string (e.g. "BCM2711")
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

    if (soc_id_cached) {
        strncpy(socID, cached_soc_id, BUFFER_SIZE - 1);
        socID[BUFFER_SIZE - 1] = '\0';
        hal_dbg("SOC ID is %s (cached)\n", socID);
        return dsERR_NONE;
    }

    const char *compatible_paths[] = {
        SYS_DT_COMPATIBLE,
        PROC_DT_COMPATIBLE
    };
    const size_t compatible_paths_count =
        sizeof(compatible_paths) / sizeof(compatible_paths[0]);
    const size_t prefix_len = strlen(SOC_COMPATIBLE_PREFIX);
    char cbuf[BUFFER_SIZE] = {0};

    for (size_t path_idx = 0; path_idx < compatible_paths_count; ++path_idx) {
        FILE *fp = fopen(compatible_paths[path_idx], "rb");
        if (fp == NULL) {
            hal_warn("Unable to open compatible source '%s'\n",
                     compatible_paths[path_idx]);
            continue;
        }

        memset(cbuf, 0, sizeof(cbuf));
        size_t len = fread(cbuf, 1, BUFFER_SIZE - 1, fp);
        fclose(fp);

        if (len == 0) {
            hal_warn("No data read from compatible source '%s'\n",
                     compatible_paths[path_idx]);
            continue;
        }

        size_t pos = 0;
        while (pos < len) {
            size_t token_len = dsBoundedStrLen(&cbuf[pos], len - pos);

            if (token_len == 0) {
                ++pos;
                continue;
            }

            if (token_len >= prefix_len &&
                strncmp(&cbuf[pos], SOC_COMPATIBLE_PREFIX, prefix_len) == 0) {
                const char *token_ptr = &cbuf[pos];
                const char *comma = memchr(token_ptr, ',', token_len);
                const char *chip_name = (comma != NULL) ? (comma + 1) : token_ptr;
                size_t chip_len = token_len - (size_t)(chip_name - token_ptr);
                for (size_t i = 0; i < chip_len; ++i) {
                    socID[i] = (char)toupper((unsigned char)chip_name[i]);
                }
                socID[chip_len] = '\0';
                strncpy(cached_soc_id, socID, BUFFER_SIZE - 1);
                cached_soc_id[BUFFER_SIZE - 1] = '\0';
                soc_id_cached = true;

                hal_dbg("SOC ID is %s\n", socID);
                return dsERR_NONE;
            }

            pos += token_len + 1;
        }
    }

    hal_err("Unable to determine SoC ID from compatible entries\n");
    return dsERR_GENERAL;
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
    hal_warn("invoked; tvservice removed - returning default GPU memory estimate.\n");
    if (memory == NULL) {
        hal_err("Invalid parameter, memory(%p)\n", memory);
        return dsERR_INVALID_PARAM;
    }
    /* GPU memory query via tvservice removed (Pi4 GPU memory is fixed at boot). */
    /* Return a sensible default estimate: 128MB free GPU memory (typical on Pi4). */
    *memory = 128 * 1024 * 1024;  /* 128 MB in bytes */
    hal_info("Returning default GPU free memory: %" PRIu64 " bytes\n", *memory);
    return dsERR_NONE;
}

dsError_t dsGetTotalSystemGraphicsMemory(uint64_t *memory)
{
    hal_warn("invoked; deprecated ?.\n");
    if (memory == NULL) {
        hal_err("Invalid parameter, memory(%p)\n", memory);
        return dsERR_INVALID_PARAM;
    }
    /* GPU memory query via tvservice removed (Pi4 GPU memory is fixed at boot). */
    /* Return a sensible default estimate: 256MB total GPU memory (typical on Pi4). */
    *memory = 256 * 1024 * 1024;  /* 256 MB in bytes */
    hal_info("Returning default GPU total memory: %" PRIu64 " bytes\n", *memory);
    return dsERR_NONE;
}
