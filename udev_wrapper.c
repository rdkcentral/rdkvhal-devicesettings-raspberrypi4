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
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "dshalLogger.h"

volatile int exit_udev_mon_thread = 0;

void get_hdmi_status_libdrm(const char *devnode) {
	if (!devnode) {
		hal_err("Invalid argument\n");
		return;
	}
	const char *card = strrchr(devnode, '/');
	if (!card) {
		hal_err("Invalid device node: %s\n", devnode);
		return;
	}
	card++;
	char card_path[32] = {0};
	snprintf(card_path, sizeof(card_path), "/dev/dri/%s", card);
	hal_dbg("Opening DRM device: %s\n", card_path);
	int fd = open(card_path, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		hal_err("Failed to open DRM device: %s\n", card_path);
		return;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources\n");
		close(fd);
		return;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector\n");
			continue;
		}

		if (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
		    connector->connector_type == DRM_MODE_CONNECTOR_HDMIB) {
			const char *status =
			    (connector->connection == DRM_MODE_CONNECTED)
			        ? "connected"
			        : "disconnected";
			hal_dbg("HDMI connector %d status: %s\n",
			        connector->connector_id, status);
		}

		drmModeFreeConnector(connector);
	}

	drmModeFreeResources(resources);
	close(fd);
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

	while (!exit_udev_mon_thread) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		tv.tv_sec = 0;
		tv.tv_usec = 250000;

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
					if (strstr(devnode, "card")) {
						if (callback) {
							callback(devnode);
						} else {
							/* If no external
							 * callback registered,
							 * use internal. */
							get_hdmi_status_libdrm(
							    devnode);
						}
					}
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

void signal_udevmon_exit() { exit_udev_mon_thread = 1; }
