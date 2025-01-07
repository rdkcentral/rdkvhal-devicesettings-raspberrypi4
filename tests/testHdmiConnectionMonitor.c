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

#include "udev_wrapper.h"

pthread_t thread_id;

void print_hdmi_status(const char *devnode) {
	if (devnode) {
		printf("HDMI connector status changed: '%s'\n", devnode);
	} else {
		printf("HDMI connector status changed: Unknown device\n");
	}
}

int main(void) {
	printf("Sample app listener for HDMI connection status changes.\n");
	if (pthread_create(&thread_id, NULL, monitor_hdmi_status_changes,
	                   (void *)print_hdmi_status) != 0) {
		fprints(stderr, "Failed to create thread: %s\n",
		        strerror(errno));
		return 1;
	} else {
		printf("Waiting 10s for HDMI status change.\n");
	}
	sleep(10);
	signal_udevmon_exit();
	pthread_join(thread_id, NULL);
	return 0;
}
