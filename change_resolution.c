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

void change_resolution(int fd, drmModeConnector *connector, drmModeRes *resources, int interval)
{
    for (int i = 0; i < connector->count_modes; i++) {
        drmModeModeInfo mode = connector->modes[i];
        drmModeCrtc *crtc = drmModeGetCrtc(fd, resources->crtcs[0]);

        if (!crtc) {
            fprintf(stderr, "Failed to get CRTC: %s\n", strerror(errno));
            continue;
        }

        int ret = drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, 0, 0, &connector->connector_id, 1, &mode);
        if (ret) {
            fprintf(stderr, "Failed to set mode: %s\n", strerror(errno));
        } else {
            printf("Changed resolution to %dx%d\n", mode.hdisplay, mode.vdisplay);
            sleep(interval);
        }

        drmModeFreeCrtc(crtc);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <DRM_DEVICE> <INTERVAL(in sec)>\n", argv[0]);
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