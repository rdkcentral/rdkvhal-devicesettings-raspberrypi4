/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

void print_edid(int fd, drmModeConnector *connector) {
    if (connector->connection != DRM_MODE_CONNECTED) {
        printf("HDMI is not connected.\n");
        return;
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
            edid_blob = drmModeGetPropertyBlob(fd, connector->prop_values[i]);
            if (edid_blob) {
                printf("Host EDID:\n");
                for (int j = 0; j < edid_blob->length; j++) {
                    printf("%02x ", ((unsigned char*)edid_blob->data)[j]);
                    if ((j + 1) % 16 == 0) {
                        printf("\n");
                    }
                }
                printf("\n");
                drmModeFreePropertyBlob(edid_blob);
            } else {
                printf("EDID read error.\n");
            }
        }
        drmModeFreeProperty(property);
    }

    if (!has_edid) {
        printf("No EDID property found.\n");
    }
}

void change_resolution(int fd, drmModeConnector *connector, drmModeRes *resources, int interval) {
    for (int i = 0; i < connector->count_modes; i++) {
        drmModeModeInfo mode = connector->modes[i];
        drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[0]);

        if (!crtc) {
            fprintf(stderr, "Failed to get CRTC: %s\n", strerror(errno));
            continue;
        }

        // Create a dumb buffer
        struct drm_mode_create_dumb create_dumb = {0};
        create_dumb.width = mode.hdisplay;
        create_dumb.height = mode.vdisplay;
        create_dumb.bpp = 32; // Bits per pixel
        if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_dumb) < 0) {
            fprintf(stderr, "Failed to create dumb buffer for mode %dx%d: %s\n", mode.hdisplay, mode.vdisplay, strerror(errno));
            drmModeFreeCrtc(crtc);
            continue;
        }

        // Map the dumb buffer
        struct drm_mode_map_dumb map_dumb = {0};
        map_dumb.handle = create_dumb.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map_dumb) < 0) {
            fprintf(stderr, "Failed to map dumb buffer for mode %dx%d: %s\n", mode.hdisplay, mode.vdisplay, strerror(errno));
            drmModeFreeCrtc(crtc);
            struct drm_mode_destroy_dumb destroy_dumb = {0};
            destroy_dumb.handle = create_dumb.handle;
            drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
            continue;
        }

        // Set the CRTC
        int ret = drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, 0, 0, &connector->connector_id, 1, &mode);
        if (ret) {
            fprintf(stderr, "Failed to set mode %dx%d: %s\n", mode.hdisplay, mode.vdisplay, strerror(errno));
        } else {
            printf("Changed resolution to %dx%d\n", mode.hdisplay, mode.vdisplay);
            sleep(interval);
        }

        // Clean up
        drmModeFreeCrtc(crtc);
        struct drm_mode_destroy_dumb destroy_dumb = {0};
        destroy_dumb.handle = create_dumb.handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_dumb);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <DRM_DEVICE> <INTERVAL>\n", argv[0]);
        return 1;
    }

    const char *drm_device = argv[1];
    int interval = atoi(argv[2]);
    if (interval <= 0) {
        fprintf(stderr, "Invalid interval: %s\n", argv[2]);
        return 1;
    }

    int fd = open(drm_device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "Failed to open DRM device %s: %s\n", drm_device, strerror(errno));
        return 1;
    }

    drmModeRes *resources = drmModeGetResources(fd);
    if (!resources) {
        fprintf(stderr, "Failed to get DRM resources: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    for (int i = 0; i < resources->count_connectors; i++) {
        drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i]);
        if (!connector) {
            fprintf(stderr, "Failed to get connector: %s\n", strerror(errno));
            continue;
        }

        if (connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            print_edid(fd, connector);
            change_resolution(fd, connector, resources, interval);
        } else {
            fprintf(stderr, "Connector %d is not connected or has no modes\n", connector->connector_id);
        }

        drmModeFreeConnector(connector);
    }

    drmModeFreeResources(resources);
    close(fd);
    return 0;
}