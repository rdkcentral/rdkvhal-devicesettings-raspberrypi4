/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2019 RDK Management
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
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "dshalUtils.h"
#include "dshalLogger.h"

static uint16_t initialised = 0;
VCHI_INSTANCE_T vchi_instance;
VCHI_CONNECTION_T *vchi_connection;

int vchi_tv_init()
{
    hal_info("invoked.\n");
    int res = 0;
    if (!initialised)
    {
        vcos_init();
        res = vchi_initialise(&vchi_instance);
        if (res != 0)
        {
            hal_err("Failed to initialize VCHI (res=%d)\n", res);
            return res;
        }

        res = vchi_connect(NULL, 0, vchi_instance);
        if (res != 0)
        {
            hal_err("Failed to create VCHI connection (ret=%d)\n", res);
            return res;
        }

        // Initialize the tvservice
        vc_vchi_tv_init(vchi_instance, &vchi_connection, 1);
        // Initialize the gencmd
        vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1);
        initialised = 1;
    }
    return res;
}

int vchi_tv_uninit()
{
    hal_info("invoked.\n");
    int res = 0;
    if (initialised)
    {
        // Stop the tvservice
        vc_vchi_tv_stop();
        vc_gencmd_stop();
        // Disconnect the VCHI connection
        vchi_disconnect(vchi_instance);
        initialised = 0;
    }
    return res;
}

static int detailedBlock(unsigned char *x, int extension, dsDisplayEDID_t *displayEdidInfo)
{
    hal_info("extension %d\n", extension);
    static unsigned char name[53];
    switch (x[3]) {
        case 0xFC:
            if (strchr((char *)name, '\n'))
                return 1;
            strncat((char *)name, (char *)x + 5, 13);
            strncpy(displayEdidInfo->monitorName, (const char *)name, dsEEDID_MAX_MON_NAME_LENGTH);
            return 1;
        default:
            return 1;
    }
}

static void hdmi_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    displayEdidInfo->physicalAddressA = (x[4] >> 4);
    displayEdidInfo->physicalAddressB = (x[4] & 0x0f);
    displayEdidInfo->physicalAddressC = (x[5] >> 4);
    displayEdidInfo->physicalAddressD = (x[5] & 0x0f);
    if (displayEdidInfo->physicalAddressB)
        displayEdidInfo->isRepeater = true;
    else
        displayEdidInfo->isRepeater = false;
}

static void cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    unsigned int oui;
    switch ((x[0] & 0xe0) >> 5) {
        case 0x03:
            oui = (x[3] << 16) + (x[2] << 8) + x[1];
            if (oui == 0x000c03) {
                hdmi_cea_block(x, displayEdidInfo);
                displayEdidInfo->hdmiDeviceType = true;
            }
            break;
        default:
            break;
    }
}

static int parse_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    int ret = 0;
    int version = x[1];
    int offset = x[2];
    unsigned char *detailed_buf;
     if (version == 3) {
            for (int i = 4; i < offset; i += (x[i] & 0x1f) + 1) {
                cea_block(x + i, displayEdidInfo);
            }
    }
    for (detailed_buf = x + offset; detailed_buf + 18 < x + 127; detailed_buf += 18)
        if (detailed_buf[0])
            detailedBlock(detailed_buf, 1, displayEdidInfo);
    return ret;
}

int fill_edid_struct(unsigned char *edidBytes, dsDisplayEDID_t *displayEdidInfo, int size)
{
    hal_info("invoked.\n");
    unsigned char *x;
    time_t t;
    struct tm *localtm;
    int i;
    if (!edidBytes || memcmp(edidBytes, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00", 8))
    {
        hal_dbg("Header not found\n");
        return -1;
    }
    displayEdidInfo->productCode = edidBytes[0x0A] + (edidBytes[0x0B] << 8);
    displayEdidInfo->serialNumber = (edidBytes[0x0C] + (edidBytes[0x0D] << 8) + (edidBytes[0x0E] << 16) + (edidBytes[0x0F] << 24));
    displayEdidInfo->hdmiDeviceType = true; // This is true for Rpi
    time(&t);
    localtm = localtime(&t);
    if (edidBytes[0x10] < 55 || edidBytes[0x10] == 0xff)
    {
        if (edidBytes[0x11] > 0x0f)
        {
            if (edidBytes[0x10] == 0xff)
            {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11];
            }
            else if (edidBytes[0x11] + 90 <= localtm->tm_year)
            {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11] + 1990;
            }
        }
    }
    detailedBlock(edidBytes + 0x36, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x48, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x5A, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x6C, 0, displayEdidInfo);
    x = edidBytes;
    for (i = 128; i < size; i += 128) {
        if (x[0] == 0x02)
           parse_cea_block(x, displayEdidInfo);
    }
    return 0;
}

bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize)
{
    char buffer[256] = {0};
    if (NULL == cmd || NULL == resp) {
        return false;
    }
    FILE *fp = popen(cmd, "r");
    if (NULL != fp) {
        size_t totalLen = 0;
        while (fgets(buffer, sizeof(buffer), fp) != NULL) {
            printf("Read '%s'\n", buffer);
            size_t len = strlen(buffer);
            if (totalLen + len < respSize - 1) {
                strncpy(resp + totalLen, buffer, len);
                totalLen += len;
            } else {
                hal_warn("Response buffer is overflowing.\n");
                strncpy(resp + totalLen, buffer, respSize - totalLen - 1);
                totalLen = respSize - 1;
                break;
            }
        }
        resp[totalLen] = '\0';
        pclose(fp);
        return true;
    }
    return false;
}

const dsVideoResolution_t* getResolutionFromVic(int vic)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].vic == vic) {
            return &vicMapTable[i].resolution;
        }
    }
    return NULL; // VIC not found
}

const int* getVicFromResolution(dsVideoResolution_t resolution)
{
    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
        if (vicMapTable[i].resolution == resolution) {
            return &vicMapTable[i].vic;
        }
    }
    return NULL; // VIC not found
}
