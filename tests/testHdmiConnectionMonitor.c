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

#include <errno.h>
#include <libudev.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "udev_wrapper.h"

pthread_t thread_id;

void print_hdmi_status(const char *devnode) {
	if (!devnode) {
		printf("Invalid argument\n");
		return;
	}
	printf("HDMI status changed: %s\n", devnode);
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <INTERVAL>\n", argv[0]);
		return 1;
	}
	int interval = atoi(argv[1]);
	if (interval <= 0) {
		fprintf(stderr, "Invalid interval: %s\n", argv[1]);
		return 1;
	}
	printf("Sample app listener for HDMI connection status changes.\n");
	/* pass the CB function as arg at NULL to receive the callback */
	if (pthread_create(&thread_id, NULL, monitor_hdmi_status_changes,
	                   (void *)NULL) != 0) {
		fprintf(stderr, "Failed to create thread: %s\n",
		        strerror(errno));
		return 1;
	} else {
		printf("Waiting %ds for HDMI status change.\n", interval);
	}
	sleep(interval);
	signal_udevmon_exit();
	pthread_join(thread_id, NULL);
	return 0;
}
