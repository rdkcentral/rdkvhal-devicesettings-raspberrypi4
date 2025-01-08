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

#include "udev_wrapper.h"

#include <errno.h>
#include <libudev.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dshalLogger.h"

volatile int exit_udev_mon_thread = 0;

void get_hdmi_status(const char *devpath) {
	if (!devpath) {
		hal_err("Invalid device path\n");
		return;
	}
	hal_dbg("Getting HDMI status for device: %s\n", devpath);
	char status_path[256];
	snprintf(status_path, sizeof(status_path), "%s/status", devpath);
	hal_dbg("Reading HDMI status from file: %s\n", status_path);
	FILE *file = fopen(status_path, "r");
	if (file == NULL) {
		hal_err("Failed to open status file: %s\n", status_path);
		return;
	}

	char status[32] = {0};
	if (fgets(status, sizeof(status), file) != NULL) {
		status[strcspn(status, "\n")] = '\0';
		hal_dbg("HDMI status for %s: %s\n", status_path, status);
	} else {
		hal_err("Failed to read status from file: %s\n", status_path);
	}

	fclose(file);
}

void enumerate_hdmi_connectors(struct udev *udev) {
	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	if (!enumerate) {
		hal_err("Failed to create udev enumerate object\n");
		return;
	}

	udev_enumerate_add_match_subsystem(enumerate, "drm");
	udev_enumerate_scan_devices(enumerate);

	struct udev_list_entry *devices =
	    udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry *entry;

	udev_list_entry_foreach(entry, devices) {
		const char *path = udev_list_entry_get_name(entry);
		struct udev_device *dev =
		    udev_device_new_from_syspath(udev, path);

		if (dev) {
			const char *devtype = udev_device_get_devtype(dev);
			if (devtype && strcmp(devtype, "drm_minor") == 0) {
				const char *devnode =
				    udev_device_get_devnode(dev);
				if (devnode) {
					hal_dbg("Found DRM device: %s\n",
					        devnode);
					if (strstr(devnode, "HDMI")) {
						hal_dbg(
						    "Found HDMI connector: "
						    "%s\n",
						    devnode);
						get_hdmi_status(
						    udev_device_get_syspath(
						        dev));
					} else {
						hal_dbg(
						    "Non-HDMI DRM device: %s\n",
						    devnode);
					}
				} else {
					hal_dbg("DRM device without devnode\n");
				}
			} else {
				hal_dbg("Non-DRM device type: %s\n", devtype);
			}
			udev_device_unref(dev);
		} else {
			hal_err(
			    "Failed to create udev device from syspath: %s\n",
			    path);
		}
	}

	udev_enumerate_unref(enumerate);
}

void *monitor_hdmi_status_changes(void *arg) {
	if (!arg) {
		hal_err("Invalid argument\n");
		return NULL;
	}
	hdmi_status_callback_t callback = (hdmi_status_callback_t)arg;
	struct udev *udev;
	struct udev_monitor *mon;
	int fd;
	fd_set fds;
	struct timeval tv;
	int ret;

	udev = udev_new();
	if (!udev) {
		hal_err("Failed to create udev object\n");
		return NULL;
	}

	mon = udev_monitor_new_from_netlink(udev, "udev");
	if (!mon) {
		hal_err("Failed to create udev monitor\n");
		udev_unref(udev);
		return NULL;
	}

	if (udev_monitor_filter_add_match_subsystem_devtype(mon, "drm",
	                                                    "drm_minor") < 0) {
		hal_err("Failed to add filter for DRM events\n");
		udev_monitor_unref(mon);
		udev_unref(udev);
		return NULL;
	}

	if (udev_monitor_enable_receiving(mon) < 0) {
		hal_err("Failed to enable receiving udev events\n");
		udev_monitor_unref(mon);
		udev_unref(udev);
		return NULL;
	}

	fd = udev_monitor_get_fd(mon);
	if (fd < 0) {
		hal_err("Failed to get file descriptor for udev monitor\n");
		udev_monitor_unref(mon);
		udev_unref(udev);
		return NULL;
	}

	// Initial enumeration of HDMI connectors
	enumerate_hdmi_connectors(udev);

	while (!exit_udev_mon_thread) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;  // 250ms timeout

		ret = select(fd + 1, &fds, NULL, NULL, &tv);
		if (ret > 0 && FD_ISSET(fd, &fds)) {
			struct udev_device *dev =
			    udev_monitor_receive_device(mon);
			if (dev) {
				const char *action =
				    udev_device_get_action(dev);
				const char *devnode =
				    udev_device_get_devnode(dev);
				if (action && (strcmp(action, "change") == 0)) {
					callback(devnode);
					enumerate_hdmi_connectors(udev);
				}
				udev_device_unref(dev);
			}
		} else if (ret < 0) {
			hal_err("Error in select(): %s\n", strerror(errno));
			break;
		}
	}

	hal_dbg("Exiting HDMI status monitoring thread\n");

	udev_monitor_unref(mon);
	udev_unref(udev);
	return NULL;
}

void print_hdmi_status(const char *devnode) {
	if (devnode) {
		hal_dbg("HDMI connector status changed: '%s'\n", devnode);
	} else {
		hal_dbg("HDMI connector status changed: Unknown device\n");
	}
}

void signal_udevmon_exit() { exit_udev_mon_thread = 1; }
