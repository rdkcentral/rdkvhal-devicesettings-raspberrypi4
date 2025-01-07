/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2025 RDK Management
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

#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <stdbool.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define EDID_LENGTH 128
#define EDID_HEADER "\x00\xff\xff\xff\xff\xff\xff\x00"

#ifndef DRI_CARD
#error "DRI_CARD is not defined"
#endif

bool validate_edid(unsigned char *edid, int length);
bool print_edid(int fd, drmModeConnector *connector);
bool print_dri_edid(void);
bool print_connected_display_edid(void);
bool list_connector_status(void);
bool print_supported_resolutions(void);
bool change_resolution(void);
int open_drm_device();
