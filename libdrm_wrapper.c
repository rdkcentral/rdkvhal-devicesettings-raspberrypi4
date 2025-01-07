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
 * @param fd The file descriptor of the DRM device.
 * @param connector The connector.
 * @return true if success, false otherwise.
 */
bool print_edid(int fd, drmModeConnector *connector) {
	if (NULL == connector) {
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
					hal_dbg("%02x ",
					        ((unsigned char *)
					             edid_blob->data)[j]);
					if ((j + 1) % 16 == 0) {
						hal_dbg("\n");
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
	int fd = open_drm_device();
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
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
	close(fd);
	return true;
}

/**
 * @brief Print the EDID data of the connected display.
 * @return true if success, false otherwise.
 */
bool print_connected_display_edid(void) {
	int fd = open_drm_device();
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		hal_err("Failed to get DRM resources: '%s'\n", strerror(errno));
		close(fd);
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
	close(fd);
	return true;
}

/**
 * @brief List the status of the connector.
 * @return true if success, false otherwise.
 */
bool list_connector_status(void) {
	int fd = open_drm_device();
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "Failed to get DRM resources: %s\n",
		        strerror(errno));
		close(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			fprintf(stderr, "Failed to get connector: %s\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Connector %d: '%s'\n", connector->connector_id,
		        connector->connection == DRM_MODE_CONNECTED
		            ? "connected"
		            : "disconnected");
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
	close(fd);
	return true;
}

/**
 * @brief Print the supported resolutions for the connector.
 * @return true if success, false otherwise.
 */
bool print_supported_resolutions(void) {
	int fd = open_drm_device();
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "Failed to get DRM resources: %s\n",
		        strerror(errno));
		close(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			fprintf(stderr, "Failed to get connector: %s\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Supported resolutions for connector %d:\n",
		        connector->connector_id);
		for (int i = 0; i < connector->count_modes; i++) {
			drmModeModeInfo mode = connector->modes[i];
			hal_dbg("  %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay,
			        mode.vrefresh);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close(fd);
	return true;
}

bool change_resolution(int interval) {
	int fd = open_drm_device();
	if (fd < 0) {
		hal_err("Failed to open DRM device\n");
		return false;
	}
	drmModeRes *resources = drmModeGetResources(fd);
	if (!resources) {
		fprintf(stderr, "Failed to get DRM resources: %s\n",
		        strerror(errno));
		close(fd);
		return false;
	}

	for (int i = 0; i < resources->count_connectors; i++) {
		drmModeConnector *connector =
		    drmModeGetConnector(fd, resources->connectors[i]);
		if (!connector) {
			fprintf(stderr, "Failed to get connector: %s\n",
			        strerror(errno));
			continue;
		}
		hal_dbg("Supported resolutions for connector %d:\n",
		        connector->connector_id);
		for (int i = 0; i < connector->count_modes; i++) {
			drmModeModeInfo mode = connector->modes[i];
			hal_dbg("  %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay,
			        mode.vrefresh);
		}
		if (connector->connection == DRM_MODE_CONNECTED &&
		    connector->count_modes > 0) {
			for (int i = 0; i < connector->count_modes; i++) {
				drmModeModeInfo mode = connector->modes[i];
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
					    "mode %dx%d: "
					    "'%s'\n",
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

				hal_err("Mode %d: %dx%d@%d-%d\n", i,
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
			fprintf(
			    stderr,
			    "Connector %d is not connected or has no modes\n",
			    connector->connector_id);
		}
		drmModeFreeConnector(connector);
	}
	drmModeFreeResources(resources);
	close(fd);
	return true;
}

/**
 * @brief Open the DRM device.
 * @return The file descriptor of the DRM device, or -1 on error.
 */
int open_drm_device() {
	int fd = open(DRI_CARD, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		hal_err("Failed to open DRM device: '%s'\n", strerror(errno));
		return -1;
	}
	return fd;
}
