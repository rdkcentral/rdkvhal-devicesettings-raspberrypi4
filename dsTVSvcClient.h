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
 * dsTVSvcClient.h
 *
 * IPC client interface used by the ds-hal library to communicate with the
 * TVService broker daemon (dsTVSvcDaemon).
 *
 * API signatures deliberately mirror the underlying vc_tv_* functions so that
 * call-site changes in dsDisplay.c / dsVideoPort.c / dsAudio.c are minimal
 * (rename only, no signature changes needed for most calls).
 */

#ifndef __DSTVSVC_CLIENT_H
#define __DSTVSVC_CLIENT_H

#include <stdint.h>
#include "dsTVSvcProto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Lifecycle — called from tvsvc_acquire() / tvsvc_release() in dshalUtils.
 * ================================================================ */

/**
 * Connect to the TVService daemon and start the background event reader.
 * Idempotent: safe to call multiple times; only the first call connects.
 * Returns 0 on success, negative errno on failure.
 */
int tvsvc_client_connect(void);

/**
 * Disconnect from the TVService daemon and stop the background event reader.
 * Idempotent: safe to call when already disconnected.
 */
void tvsvc_client_disconnect(void);

/* ================================================================
 * Event subscription — replaces vc_tv_register_callback.
 * ================================================================ */

/** Callback type — same signature as VC_TV_CALLBACK_T. */
typedef void (*tvsvc_client_cb_t)(void *userdata,
                                  uint32_t reason,
                                  uint32_t param1,
                                  uint32_t param2);

/**
 * Register a local event handler.  Replaces vc_tv_register_callback().
 * Returns 0 on success, -EINVAL if cb is NULL, -ENOMEM if callback table full.
 */
int  tvsvc_client_register_callback(tvsvc_client_cb_t cb, void *userdata);

/**
 * Unregister a previously registered callback.
 * Replaces vc_tv_unregister_callback().
 */
void tvsvc_client_unregister_callback(tvsvc_client_cb_t cb);

/* ================================================================
 * Query / action — API mirrors the vc_tv_* originals exactly so
 * call sites need only a function rename, not a signature change.
 * ================================================================ */

/** Mirrors vc_tv_get_display_state().  Returns 0 on success. */
int tvsvc_client_get_display_state(TV_DISPLAY_STATE_T *state);

/**
 * Mirrors vc_tv_hdmi_get_supported_modes_new().
 * Returns number of modes on success, negative on error.
 */
int tvsvc_client_get_supported_modes(HDMI_RES_GROUP_T        group,
                                     TV_SUPPORTED_MODE_NEW_T *supported_modes,
                                     uint32_t                 max_supported_modes,
                                     HDMI_RES_GROUP_T        *preferred_group,
                                     uint32_t                *preferred_mode);

/**
 * Mirrors vc_tv_hdmi_ddc_read().
 * Returns actual bytes read on success, negative on error.
 */
int tvsvc_client_ddc_read(uint32_t offset, uint32_t length, void *buffer);

/**
 * Mirrors vc_tv_hdmi_audio_supported().
 * Returns 0 if the format/channel/rate combination is supported.
 */
int tvsvc_client_audio_supported(EDID_AudioFormat    format,
                                 int                   num_channels,
                                 EDID_AudioSampleRate sample_rate,
                                 EDID_AudioSampleSize sample_size);

/** Mirrors vc_tv_sdtv_power_on().  Returns 0 on success. */
int tvsvc_client_sdtv_power_on(SDTV_MODE_T mode, const SDTV_OPTIONS_T *options);

/** Mirrors vc_tv_power_off().  Returns 0 on success. */
int tvsvc_client_tv_power_off(void);

/** Mirrors vc_tv_hdmi_power_on_preferred().  Returns 0 on success. */
int tvsvc_client_hdmi_power_on_preferred(void);

/** Returns free graphics memory in the same units as vc_gencmd get_mem reloc. */
int tvsvc_client_get_free_graphics_memory(uint64_t *memory);

/** Returns total graphics memory in the same units as vc_gencmd get_mem reloc_total. */
int tvsvc_client_get_total_graphics_memory(uint64_t *memory);

#ifdef __cplusplus
}
#endif

#endif /* __DSTVSVC_CLIENT_H */
