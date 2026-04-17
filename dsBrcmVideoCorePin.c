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

/**
 * @file dsBrcmVideoCorePin.c
 * @brief Broadcom VideoCore library pinning support for RDK Device Settings HAL
 * 
 * This module ensures libvcos.so remains loaded throughout the HAL lifecycle
 * to prevent TLS (Thread-Local Storage) destructor crashes that occur when
 * the library is prematurely unloaded while client processes still hold
 * TLS references.
 * 
 * The library is pinned using RTLD_NODELETE flag during library construction
 * and properly released during library destruction.
 */

#include <dlfcn.h>
#include "dshalLogger.h"

#ifdef DSHAL_ENABLE_VCOS_PIN

/**
 * @brief Handle to pinned libvcos.so library
 * 
 * This handle is obtained during library construction and ensures the
 * Broadcom VideoCore library remains loaded to prevent TLS crashes.
 */
static void *g_libvcosHandle = NULL;

/**
 * @brief Library constructor - Pins libvcos.so on HAL initialization
 * 
 * This function runs automatically when libdshal.so is loaded.
 * It pins libvcos.so in memory using RTLD_NODELETE to prevent it from
 * being unloaded prematurely, which would cause TLS destructor crashes
 * in client processes.
 * 
 * @note This runs before any HAL module initialization functions
 */
__attribute__((constructor))
static void dshal_broadcom_init(void)
{
    const char *dlError = NULL;

    // Pin libvcos.so to prevent TLS destructor crashes
    // RTLD_NODELETE ensures the library cannot be unloaded via dlclose()
    dlerror(); // Clear any stale dynamic loader error state
    g_libvcosHandle = dlopen("libvcos.so", RTLD_LAZY | RTLD_NODELETE);
    
    if (g_libvcosHandle) {
        hal_info("[Constructor] libvcos.so pinned successfully (handle=%p)\n", 
                 (void*)g_libvcosHandle);
    } else {
        dlError = dlerror();
        hal_err("[Constructor] Failed to pin libvcos.so: %s\n",
                dlError ? dlError : "unknown dlopen error");
    }
}

/**
 * @brief Library destructor - Releases libvcos.so handle on HAL termination
 * 
 * This function runs automatically when libdshal.so is unloaded.
 * It releases the dlopen handle to libvcos.so, though the library
 * remains loaded due to RTLD_NODELETE.
 * 
 * @note This runs after all HAL module termination functions
 */
__attribute__((destructor))
static void dshal_broadcom_cleanup(void)
{
    if (g_libvcosHandle) {
        hal_info("[DESTRUCTOR] Releasing libvcos.so handle\n");
        dlclose(g_libvcosHandle);
        g_libvcosHandle = NULL;
    }
}

#endif /* DSHAL_ENABLE_VCOS_PIN */
