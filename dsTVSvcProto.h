/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management
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

/*
 * dsTVSvcProto.h
 *
 * Wire protocol shared between the TVService broker daemon (dsTVSvcDaemon)
 * and the HAL-library IPC client (dsTVSvcClient).
 *
 * Transport : AF_UNIX SOCK_STREAM at TVSVC_SOCK_PATH
 * Framing   : fixed 6-byte header followed by payload
 *
 * Message header (6 bytes, no padding):
 *   [version:u8][cmd:u8][req_id:u16-LE][len:u16-LE]
 *
 * Responses:  cmd field = (original_cmd | TVSVC_RESP_FLAG), req_id echoed
 * Event push: cmd field has bit-7 set (>= 0x80), req_id == 0, no reply needed
 */

#ifndef __DSTVSVCPROTO_H
#define __DSTVSVCPROTO_H

#include <stdint.h>
#include "interface/vmcs_host/vc_tvservice.h"
#include "interface/vmcs_host/vc_hdmi.h"

/* ---- Socket path ---- */
#define TVSVC_SOCK_PATH   "/run/dshal-tvsvc.sock"

/* ---- Protocol version ---- */
#define TVSVC_VERSION     1U

/* ---- Commands (client → daemon) ---- */
#define TVSVC_CMD_GET_DISPLAY_STATE        0x01U
#define TVSVC_CMD_GET_SUPPORTED_MODES      0x02U
#define TVSVC_CMD_DDC_READ                 0x03U
#define TVSVC_CMD_AUDIO_SUPPORTED          0x04U
#define TVSVC_CMD_SDTV_POWER_ON            0x05U
#define TVSVC_CMD_TV_POWER_OFF             0x06U
#define TVSVC_CMD_HDMI_POWER_ON_PREFERRED  0x07U
#define TVSVC_CMD_SUBSCRIBE_EVENTS         0x08U
#define TVSVC_CMD_UNSUBSCRIBE_EVENTS       0x09U

/* ---- Response flag OR'd into cmd field of replies ---- */
#define TVSVC_RESP_FLAG                    0x40U

/* ---- Events pushed daemon → client (bit-7 set, no response expected) ---- */
#define TVSVC_EVT_TV_CALLBACK              0x81U

/* ---- Limits ---- */
#define TVSVC_DDC_MAX_LEN                  512U
#define TVSVC_MAX_MODES_PER_REQ            127U
#define TVSVC_MAX_CLIENTS                  16

/* ================================================================
 * Wire message header — 6 bytes, explicitly packed, no padding.
 * All multi-byte fields are little-endian (native on RPi ARM).
 * ================================================================ */
typedef struct __attribute__((packed)) {
    uint8_t  version;
    uint8_t  cmd;
    uint16_t req_id;
    uint16_t len;       /* byte count of payload that follows */
} tvsvc_msg_hdr_t;

/* ================================================================
 * Payload structs — regular alignment (same ABI on both sides).
 * ================================================================ */

/* GET_SUPPORTED_MODES request */
typedef struct {
    uint32_t group;      /* HDMI_RES_GROUP_T */
    uint32_t max_modes;
} tvsvc_req_get_modes_t;

/* GET_SUPPORTED_MODES response header (followed by count × TV_SUPPORTED_MODE_NEW_T) */
typedef struct {
    int32_t  status;
    uint32_t preferred_group;  /* HDMI_RES_GROUP_T */
    uint32_t preferred_mode;
    uint32_t count;
} tvsvc_resp_get_modes_t;

/* DDC_READ request */
typedef struct {
    uint32_t offset;
    uint32_t len;
} tvsvc_req_ddc_read_t;

/* DDC_READ response header (followed by actual_len bytes) */
typedef struct {
    int32_t status;
    int32_t actual_len;
} tvsvc_resp_ddc_read_t;

/* AUDIO_SUPPORTED request */
typedef struct {
    uint32_t format;        /* EDID_AudioFormat_t */
    uint32_t num_channels;
    uint32_t sample_rate;   /* EDID_AudioSampleRate_t */
    uint32_t sample_size;   /* EDID_AudioSampleSize_t */
} tvsvc_req_audio_supported_t;

/* SDTV_POWER_ON request */
typedef struct {
    uint32_t       mode;     /* SDTV_MODE_T */
    SDTV_OPTIONS_T options;
} tvsvc_req_sdtv_power_on_t;

/* Generic simple response (status + result integer) */
typedef struct {
    int32_t status;
    int32_t result;
} tvsvc_resp_simple_t;

/* GET_DISPLAY_STATE response */
typedef struct {
    int32_t            status;
    TV_DISPLAY_STATE_T state;
} tvsvc_resp_display_state_t;

/* Event payload pushed by daemon for every TV callback (reason, param1, param2) */
typedef struct {
    uint32_t reason;
    uint32_t param1;
    uint32_t param2;
} tvsvc_evt_tv_callback_t;

#endif /* __DSTVSVCPROTO_H */
