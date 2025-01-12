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
#include "libdrm_wrapper.h"

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

#include "dshalLogger.h"

/**
 * @brief Validate the EDID data.
 * @param edid The EDID data.
 * @param length The length of the EDID data.
 * @return true if the EDID data is valid, false otherwise.
 */
bool validate_edid(unsigned char *edid, int length) {
	if (length < EDID_LENGTH || edid == NULL) {
		hal_err("Invalid parameters\n");
		return false;
	}

	if (memcmp(edid, EDID_HEADER, strlen(EDID_HEADER)) != 0) {
		hal_err("Invalid EDID header\n");
		return false;
	}

	unsigned char checksum = 0;
	for (int i = 0; i < length; i++) {
		checksum += edid[i];
	}
	if (checksum != 0) {
		hal_err("Invalid EDID checksum\n");
		return false;
	}

	return true;
}

/**
 * @brief Print the EDID data.
 * @param fd The file descriptor of drmOpen().
 * @param connector The connector.
 * @return true if success, false otherwise.
 */
bool print_edid(int fd, drmModeConnector *connector) {
	hal_dbg("Printing DRI EDID from sysfs\n");
	// read /sys/devices/platform/gpu/drm/card1/card1-HDMI-A-1/edid file and
	// print edid data.
	FILE *fp = fopen(
	    "/sys/devices/platform/gpu/drm/card1/card1-HDMI-A-1/edid", "rb");
	if (fp != NULL) {
		unsigned char edid[MAX_EDID_LENGTH];
		size_t size =
		    fread(edid, sizeof(unsigned char), MAX_EDID_LENGTH, fp);
		if (size == EDID_LENGTH || size == MAX_EDID_LENGTH) {
			hal_dbg("Host EDID: '%s'\n", validate_edid(edid, size)
			                                 ? "valid"
			                                 : "invalid");
			for (int i = 0; i < size; i++) {
				printf("%02x ", edid[i]);
				if ((i + 1) % 16 == 0) {
					printf("\n");
				}
			}
			hal_dbg("\n");
		} else {
			hal_err("Failed to read sysfs EDID data\n");
		}
		fclose(fp);
	} else {
		hal_err("Failed to open sysfs EDID file\n");
	}
	if (connector == NULL) {
		hal_err("Invalid connector\n");
		return false;
	}
	if (connector->connection != DRM_MODE_CONNECTED) {
		hal_dbg("Connector %d is not connected\n",
		        connector->connector_id);
		return false;
	}

	drmModePropertyPtr property;
	drmModePropertyBlobPtr edid_blob;
	int has_edid = 0;

	for (int i = 0; i < connector->count_props; i++) {
		property = drmModeGetProperty(fd, connector->props[i]);
		if (!property) {
			continue;
		}

		if (strcmp(property->name, "EDID") == 0) {
			has_edid = 1;
			edid_blob = drmModeGetPropertyBlob(
			    fd, connector->prop_values[i]);
			if (edid_blob) {
				hal_dbg("Host EDID: '%s'\n",
				        validate_edid(edid_blob->data,
				                      edid_blob->length)
				            ? "valid"
				            : "invalid");
				for (int j = 0; j < edid_blob->length; j++) {
					printf("%02x ",
					       ((unsigned char *)
					            edid_blob->data)[j]);
					if ((j + 1) % 16 == 0) {
						printf("\n");
					}
				}
				hal_dbg("\n");
				drmModeFreePropertyBlob(edid_blob);
			} else {
				hal_err("EDID read error.\n");
			}
		}
		drmModeFreeProperty(property);
	}

	if (!has_edid) {
		hal_warn("No EDID property found.\n");
		return false;
	}
	return true;
}

/**
 * @brief Print the EDID data of all DRI connectors.
 * @return true if success, false otherwise.
 */
bool print_dri_edid(void) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		drmClose(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("DRI Connector %d EDID:\n", connector->connector_id);
		print_edid(fd, connector);
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	drmClose(fd);
	return true;
}

/**
 * @brief Print the EDID data of the connected display.
 * @return true if success, false otherwise.
 */
bool print_connected_display_edid(void) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		drmClose(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		if (connector->connection == DRM_MODE_CONNECTED) {
			hal_dbg("Connected Display Connector %d EDID:\n",
			        connector->connector_id);
			print_edid(fd, connector);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	drmClose(fd);
	return true;
}

/**
 * @brief Get the status of the connector.
 * @param connectedStatus The connected status, true if connected else false.
 * @return true if success, false otherwise.
 */
bool get_connector_status(bool *connectedStatus) {
	if (connectedStatus != NULL) {
		*connectedStatus = false;
	}
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Connector %d: '%s'\n", connector->connector_id,
		        connector->connection == DRM_MODE_CONNECTED
		            ? "connected"
		            : "disconnected");
		if (connectedStatus != NULL) {
			*connectedStatus =
			    connector->connection == DRM_MODE_CONNECTED;
		}
		hal_dbg("  Modes: %d\n", connector->count_modes);
		hal_dbg("  Properties: %d\n", connector->count_props);
		hal_dbg("  Encoders: %d\n", connector->count_encoders);
		hal_dbg("  Subpixel: %d\n", connector->subpixel);
		hal_dbg("  Type: %d\n", connector->connector_type);
		hal_dbg("  Size: %dx%d mm\n", connector->mmWidth,
		        connector->mmHeight);
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return true;
}

/**
 * @brief Print the supported resolutions for the connector.
 * @return true if success, false otherwise.
 */
bool print_supported_resolutions(void) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Supported resolutions for connector %d:\n",
		        connector->connector_id);
		for (int j = 0; j < connector->count_modes; j++) {
			drmModeModeInfo mode = connector->modes[j];
			hal_dbg("  %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay,
			        mode.vrefresh);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return true;
}

bool change_resolution(int interval) {
	int fd = open_drm_device_by_type(DRM_NODE_CONTROL);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Supported resolutions for connector %d:\n",
		        connector->connector_id);
		for (int j = 0; j < connector->count_modes; j++) {
			drmModeModeInfo mode = connector->modes[j];
			hal_dbg("  %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay,
			        mode.vrefresh);
		}
		if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0) {
			for (int j = 0; j < connector->count_modes; j++) {
				drmModeModeInfo mode = connector->modes[j];
				drmModeCrtc *crtc =
				    drmModeGetCrtc(fd, resources->crtcs[0]);

				if (!crtc) {
					hal_err("Failed to get CRTC: '%s'\n",
					        strerror(errno));
					continue;
				}

				struct drm_mode_create_dumb create_dumb = {0};
				create_dumb.width = mode.hdisplay;
				create_dumb.height = mode.vdisplay;
				create_dumb.bpp = 32;
				if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB,
				             &create_dumb) < 0) {
					hal_err(
					    "Failed to create dumb buffer for "
					    "mode %dx%d: '%s'\n",
					    mode.hdisplay, mode.vdisplay,
					    strerror(errno));
					drmModeFreeCrtc(crtc);
					continue;
				}

				struct drm_mode_map_dumb map_dumb = {0};
				map_dumb.handle = create_dumb.handle;
				if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB,
				             &map_dumb) < 0) {
					hal_err(
					    "Failed to map dumb buffer for "
					    "mode %dx%d: '%s'\n",
					    mode.hdisplay, mode.vdisplay,
					    strerror(errno));
					drmModeFreeCrtc(crtc);
					struct drm_mode_destroy_dumb
					    destroy_dumb = {0};
					destroy_dumb.handle =
					    create_dumb.handle;
					drmIoctl(fd,
					         DRM_IOCTL_MODE_DESTROY_DUMB,
					         &destroy_dumb);
					continue;
				}

				hal_err("Mode %d: %dx%d@%d-%d\n", j,
				        mode.hdisplay, mode.vdisplay,
				        mode.vrefresh, mode.clock);
				int ret = drmModeSetCrtc(
				    fd, crtc->crtc_id, crtc->buffer_id, 0, 0,
				    &connector->connector_id, 1, &mode);
				if (ret) {
					hal_err(
					    "Failed to set mode %dx%d: %s\n",
					    mode.hdisplay, mode.vdisplay,
					    strerror(errno));
				} else {
					hal_dbg("Changed resolution to %dx%d\n",
					        mode.hdisplay, mode.vdisplay);
					sleep(interval);
				}

				drmModeFreeCrtc(crtc);
				struct drm_mode_destroy_dumb destroy_dumb = {0};
				destroy_dumb.handle = create_dumb.handle;
				drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB,
				         &destroy_dumb);
			}
		} else {
			hal_err(
			    "Connector %d is not connected or has no modes\n",
			    connector->connector_id);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return true;
}

/**
 * @brief Open the DRI_CARD device.
 * @param [DRM_NODE_PRIMARY/DRM_NODE_CONTROL/DRM_NODE_RENDER] see xf86drm.h
 * @return The file descriptor of the DRM device, or -1 on error.
 */
int open_drm_device_by_type(int node_type) {
	int fd = open_drm_device(DRI_CARD, node_type);
	if (fd < 0) {
		hal_err("Failed to open DRM device %s, using default.\n",
		        DRI_CARD);
		fd = open_drm_device("/dev/dri/card0", node_type);
		if (fd < 0) {
			hal_err("Failed to open DRM device /dev/dri/card0\n");
			return -1;
		}
	}
	return fd;
}

/**
 * @brief Open the DRM device.
 * @param devnode The device node, e.g., /dev/dri/card0.
 * @param node_type DRM_NODE_PRIMARY/DRM_NODE_CONTROL/DRM_NODE_RENDER.
 * @return The file descriptor of the DRM device, or -1 on error.
 */
int open_drm_device(const char *devnode, int node_type) {
	if (!devnode) {
		hal_err("Invalid device node\n");
		return -1;
	}
	hal_dbg("Opening DRM device '%s' with node type %d\n", devnode,
	        node_type);
#if 0
	int fd = drmOpenWithType(devnode, NULL, node_type);
	if (fd < 0) {
		int err = errno;
		drmVersionPtr version = drmGetVersion(fd);
		if (version) {
			hal_err(
			    "Failed to open DRM device: %s, version: %s, date: "
			    "%s, desc: %s, error: %s\n",
			    devnode, version->name, version->date,
			    version->desc, strerror(err));
			drmFreeVersion(version);
		} else {
			hal_err("Failed to open DRM device: %s, error: %s\n",
			        devnode, strerror(err));
		}
		return -1;
	}
	// try to set master. May fail if another process has already set master
	if (drmSetMaster(fd) < 0) {
		int err = errno;
		hal_err("Failed to set DRM master: %s\n", strerror(err));
		drmClose(fd);
		return -1;
	}
#else
	hal_dbg("Opening DRM device witout node_type '%d'\n", node_type);
	int fd = open(devnode, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		hal_err("Failed to open DRM device: %s\n", strerror(errno));
		return -1;
	}
	drmDropMaster(fd);
#endif
	return fd;
}

/**
 * @brief Close the DRM device.
 * @param fd The file descriptor of the DRM device.
 */
void close_drm_device(int fd) {
	if (fd >= 0) {
		close(fd);
		hal_dbg("Closed DRM device with file descriptor %d\n", fd);
	}
}

/**
 * @brief Get the current resolution.
 * @param width The width of the resolution.
 * @param height The height of the resolution.
 * @param refresh_rate The refresh rate of the resolution.
 * @param mode The mode of the resolution, interlaced(0) or progressive(1).
 * @return true if success, false otherwise.
 */
bool get_current_resolution(int *width, int *height, int *refresh_rate,
                            int *mode) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[0]);
	if (!crtc) {
		hal_err("Failed to get CRTC: '%s'\n", strerror(errno));
		drmModeFreeResources(resources);
		close_drm_device(fd);
		return false;
	}
	*width = crtc->mode.hdisplay;
	*height = crtc->mode.vdisplay;
	*refresh_rate = crtc->mode.vrefresh;
	*mode = (crtc->mode.flags & DRM_MODE_FLAG_INTERLACE) ? 0 : 1;
	drmModeFreeCrtc(crtc);
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return true;
}

/**
 * @brief Check if the resolution is supported.
 * @param width The width of the resolution.
 * @param height The height of the resolution.
 * @param refresh_rate The refresh rate of the resolution.
 * @param mode The mode of the resolution, interlaced(0) or progressive(1).
 * @return true if the resolution is supported, false otherwise.
 */
bool is_supported_resolution(int width, int height, int refresh_rate,
                             int mode) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0) {
			for (int j = 0; j < connector->count_modes; j++) {
				drmModeModeInfo mode_info = connector->modes[j];
				if (mode_info.hdisplay == width &&
				    mode_info.vdisplay == height &&
				    mode_info.vrefresh == refresh_rate) {
					drmModeFreeConnector(connector);
					drmModeFreeResources(resources);
					close_drm_device(fd);
					return true;
				}
			}
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return false;
}

/**
 * @brief Set the current resolution.
 * @param width The width of the resolution.
 * @param height The height of the resolution.
 * @param refresh_rate The refresh rate of the resolution.
 * @param mode The mode of the resolution, interlaced(0) or progressive(1).
 * @return true if success, false otherwise.
 */
bool set_current_resolution(int width, int height, int refresh_rate, int mode) {
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			hal_err("Failed to get connector: '%s'\n",
			        strerror(errno));
			continue;
		}
		if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0) {
			for (int j = 0; j < connector->count_modes; j++) {
				drmModeModeInfo mode_info = connector->modes[j];
				if (mode_info.hdisplay == width &&
				    mode_info.vdisplay == height &&
				    mode_info.vrefresh == refresh_rate) {
					drmModeCrtc *crtc = drmModeGetCrtc(
					    fd, resources->crtcs[0]);
					if (!crtc) {
						hal_err(
						    "Failed to get CRTC: "
						    "'%s'\n",
						    strerror(errno));
						continue;
					}
					int ret = drmModeSetCrtc(
					    fd, crtc->crtc_id, crtc->buffer_id,
					    0, 0, &connector->connector_id, 1,
					    &mode_info);
					drmModeFreeCrtc(crtc);
					if (ret) {
						hal_err(
						    "Failed to set mode %dx%d: "
						    "%s\n",
						    width, height,
						    strerror(errno));
					} else {
						hal_dbg(
						    "Changed resolution to "
						    "%dx%d\n",
						    width, height);
						drmModeFreeConnector(connector);
						drmModeFreeResources(resources);
						close_drm_device(fd);
						return true;
					}
				}
			}
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return false;
}

/**
 * @brief Enable or disable HDMI output.
 * @param enable true to enable HDMI output, false to disable.
 * @return true if success, false otherwise.
 */
bool enable_hdmi_output(bool *enable) {
	bool ret = false;
	if (enable == NULL) {
		hal_err("Invalid parameter\n");
		return false;
	}
	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}
	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if ((connector != NULL) &&
		    (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
		     connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)) {
			uint32_t connector_id = connector->connector_id;
			drmModeObjectProperties *props =
			    drmModeObjectGetProperties(
			        fd, connector_id, DRM_MODE_OBJECT_CONNECTOR);
			if (!props) {
				hal_err("Failed to get connector properties\n");
				drmModeFreeConnector(connector);
				continue;
			}
			for (int j = 0; j < props->count_props; j++) {
				drmModePropertyPtr property =
				    drmModeGetProperty(fd, props->props[j]);
				if ((property != NULL) &&
				    (strcmp(property->name, "DPMS") == 0)) {
					uint64_t value =
					    *enable ? DRM_MODE_DPMS_ON
					            : DRM_MODE_DPMS_OFF;
					if (drmModeObjectSetProperty(
					        fd, connector_id,
					        DRM_MODE_OBJECT_CONNECTOR,
					        property->prop_id,
					        value) != 0) {
						hal_err(
						    "Failed to set DPMS "
						    "property\n");
					} else {
						hal_dbg(
						    "Set DPMS property to %s\n",
						    *enable ? "on" : "off");
						ret = true;
					}
					drmModeFreeProperty(property);
					break;
				}
				drmModeFreeProperty(property);
			}
			drmModeFreeObjectProperties(props);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close_drm_device(fd);
	return ret;
}

/**
 * @brief get HDMI output status enabled/disabled.
 * @param isEnabled true if HDMI output is enabled, false otherwise.
 * @return true if success, false otherwise.
 */
bool get_hdmi_output_status(bool *isEnabled) {
	bool ret = false;
	if (isEnabled == NULL) {
		hal_err("Invalid parameter\n");
		return false;
	}

	int fd = open_drm_device_by_type(DRM_NODE_PRIMARY);
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}

	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close_drm_device(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if ((connector != NULL) &&
		    (connector->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
		     connector->connector_type == DRM_MODE_CONNECTOR_HDMIB)) {
			uint32_t connector_id = connector->connector_id;
			drmModeObjectProperties *props =
			    drmModeObjectGetProperties(
			        fd, connector_id, DRM_MODE_OBJECT_CONNECTOR);
			if (!props) {
				hal_err("Failed to get connector properties\n");
				drmModeFreeConnector(connector);
				continue;
			}

			for (int j = 0; j < props->count_props; j++) {
				drmModePropertyPtr property =
				    drmModeGetProperty(fd, props->props[j]);
				if (property &&
				    strcmp(property->name, "DPMS") == 0) {
					uint64_t value = props->prop_values[j];
					*isEnabled =
					    (value == DRM_MODE_DPMS_ON);
					hal_dbg("Get DPMS property: %s\n",
					        *isEnabled ? "on" : "off");
					ret = true;
					drmModeFreeProperty(property);
					break;
				}
				drmModeFreeProperty(property);
			}

			drmModeFreeObjectProperties(props);
			drmModeFreeConnector(connector);
			if (ret) {
				break;
			}
		}
	}

	drmModeFreeResources(resources);
	close_drm_device(fd);
	return ret;
}
