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
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "libdrm_wrapper.h"

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <INTERVAL>\n", argv[0]);
		return 1;
	}
	int interval = atoi(argv[2]);
	if (interval <= 0) {
		fprintf(stderr, "Invalid interval: %s\n", argv[2]);
		return 1;
	}

	printf("Printing DRI EDID:\n");
	if (print_dri_edid() == false) {
		fprintf(stderr, "Failed to print DRI EDID\n");
		return 1;
	}
	printf("Printing Connected Display EDID:\n");
	if (print_connected_display_edid() == false) {
		fprintf(stderr, "Failed to print connected display EDID\n");
		return 1;
	}
	printf("Printing Connector status\n");
	if (list_connector_status() == false) {
		fprintf(stderr, "Failed to list connector status\n");
		return 1;
	}
	printf("Printing supported resolutions\n");
	if (print_supported_resolutions() == false) {
		fprintf(stderr, "Failed to print supported resolutions\n");
		return 1;
	}
	printf("Changing resolution every %d seconds\n", interval);
	if (change_resolution(interval) == false) {
		fprintf(stderr, "Failed to change resolution\n");
		return 1;
	}
	return 0;
}
