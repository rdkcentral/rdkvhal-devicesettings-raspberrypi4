/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
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

#include <sys/types.h>
#include "dsAudio.h"
#include <stdint.h>
#include <math.h>
#include <pthread.h>
#include "dsError.h"
#include "dsUtl.h"
#include "dshalUtils.h"
#include <alsa/asoundlib.h>
#include "dshalLogger.h"
#include "dsAudioSettings.h"

#define ALSA_CARD_NAME "hw:vc4hdmi0"
#define ALSA_CARD_NAME_FALLBACK "hw:vc4hdmi1"
#define ALSA_CARD_INDEX_PRIMARY "hw:0"
#define ALSA_CARD_INDEX_FALLBACK "hw:1"
#define ALSA_CARD_DEFAULT "default"
/* vc4hdmi0 exposes IEC958 controls; PCM/HDMI simple mixer elements are not present on this card. */
#define ALSA_ELEMENT_NAME "IEC958"
#define ALSA_SOFTVOL_ELEMENT_NAME "SoftMaster"
#define ALSA_IEC958_CTL_NAME "IEC958 Playback Default"

#define MAX_LINEAR_DB_SCALE 24

typedef struct _AOPHandle_t {
    dsAudioPortType_t m_vType;
    int m_index;
    int m_nativeHandle;
    bool m_IsEnabled;
} AOPHandle_t;

static AOPHandle_t _AOPHandles[dsAUDIOPORT_TYPE_MAX][2] = {};
float dBmin;
float dBmax;
static dsAudioEncoding_t _encoding = dsAUDIO_ENC_PCM;
static bool _isms11Enabled = false;
static dsAudioStereoMode_t _stereoModeHDMI = dsAUDIO_STEREO_STEREO;
static bool _bIsAudioInitialized = false;
static long _softvolSavedVolume = -1;

dsAudioOutPortConnectCB_t _halhdmiaudioCB = NULL;
dsAudioFormatUpdateCB_t _halaudioformatCB = NULL;
pthread_mutex_t gHdmiAudioCbMutex = PTHREAD_MUTEX_INITIALIZER;

static int8_t initAlsa(const char *selemname, const char *s_card, snd_mixer_t **mixer, snd_mixer_elem_t **element);
static int dsIec958CtlReadSwitch(const char *s_card, int *iec958_enabled);
static void dsCloseMixerHandle(snd_mixer_t **mixer);

static int dsInitAudioMixerElem(const char *s_card, snd_mixer_t **mixer, snd_mixer_elem_t **mixer_elem, bool *usingSoftvol)
{
    const char *softvolCandidates[] = {
        ALSA_CARD_DEFAULT,
        ALSA_CARD_NAME,
        ALSA_CARD_INDEX_PRIMARY,
        s_card,
        ALSA_CARD_NAME_FALLBACK,
        ALSA_CARD_INDEX_FALLBACK
    };

    if (usingSoftvol != NULL) {
        *usingSoftvol = false;
    }

    if ((s_card == NULL) || (mixer == NULL) || (mixer_elem == NULL)) {
        return -1;
    }

    *mixer = NULL;
    *mixer_elem = NULL;

    /* SoftMaster may be exposed on default/card index while IEC958 lives on HDMI card. */
    for (size_t i = 0; i < (sizeof(softvolCandidates) / sizeof(softvolCandidates[0])); i++) {
        const char *candidate = softvolCandidates[i];
        bool duplicate = false;

        if ((candidate == NULL) || (candidate[0] == '\0')) {
            continue;
        }

        for (size_t j = 0; j < i; j++) {
            const char *seen = softvolCandidates[j];
            if ((seen != NULL) && (strcmp(candidate, seen) == 0)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }

        if (initAlsa(ALSA_SOFTVOL_ELEMENT_NAME, candidate, mixer, mixer_elem) == 0 && (*mixer_elem != NULL)) {
            if (usingSoftvol != NULL) {
                *usingSoftvol = true;
            }
            if (strcmp(candidate, s_card) != 0) {
                hal_info("Using SoftMaster control on %s (preferred HDMI card: %s)\n", candidate, s_card);
            }
            return 0;
        }
    }

    if (initAlsa(ALSA_ELEMENT_NAME, s_card, mixer, mixer_elem) == 0 && (*mixer_elem != NULL)) {
        return 0;
    }

    if ((strcmp(s_card, ALSA_CARD_NAME) != 0) &&
            (initAlsa(ALSA_ELEMENT_NAME, ALSA_CARD_NAME, mixer, mixer_elem) == 0) && (*mixer_elem != NULL)) {
        return 0;
    }

    if ((strcmp(s_card, ALSA_CARD_NAME_FALLBACK) != 0) &&
            (initAlsa(ALSA_ELEMENT_NAME, ALSA_CARD_NAME_FALLBACK, mixer, mixer_elem) == 0) && (*mixer_elem != NULL)) {
        return 0;
    }

    if ((strcmp(s_card, ALSA_CARD_INDEX_PRIMARY) != 0) &&
            (initAlsa(ALSA_ELEMENT_NAME, ALSA_CARD_INDEX_PRIMARY, mixer, mixer_elem) == 0) && (*mixer_elem != NULL)) {
        return 0;
    }

    if ((strcmp(s_card, ALSA_CARD_INDEX_FALLBACK) != 0) &&
            (initAlsa(ALSA_ELEMENT_NAME, ALSA_CARD_INDEX_FALLBACK, mixer, mixer_elem) == 0) && (*mixer_elem != NULL)) {
        return 0;
    }

    return -1;
}

static const char *dsGetPreferredAlsaCard(void)
{
    static const char *selected_card = NULL;
    const char *hdmiCardCandidates[] = {
        ALSA_CARD_NAME,
        ALSA_CARD_NAME_FALLBACK,
        ALSA_CARD_INDEX_PRIMARY,
        ALSA_CARD_INDEX_FALLBACK
    };
    int iec958_enabled = 0;

    if (selected_card != NULL) {
        return selected_card;
    }

    for (size_t i = 0; i < (sizeof(hdmiCardCandidates) / sizeof(hdmiCardCandidates[0])); i++) {
        if (dsIec958CtlReadSwitch(hdmiCardCandidates[i], &iec958_enabled) == 0) {
            selected_card = hdmiCardCandidates[i];
            break;
        }
    }

    if (selected_card == NULL) {
        /* Prefer stable card names; retain numeric compatibility as fallback. */
        selected_card = ALSA_CARD_NAME;
    }

    hal_info("Selected ALSA HDMI card: %s\n", selected_card);
    return selected_card;
}

static dsAudioFormat_t dsFormatFromEncoding(dsAudioEncoding_t encoding)
{
    switch (encoding) {
        case dsAUDIO_ENC_PCM:
            return dsAUDIO_FORMAT_PCM;
        case dsAUDIO_ENC_AC3:
            return dsAUDIO_FORMAT_DOLBY_AC3;
        case dsAUDIO_ENC_EAC3:
            return dsAUDIO_FORMAT_DOLBY_EAC3;
        case dsAUDIO_ENC_NONE:
            return dsAUDIO_FORMAT_NONE;
        case dsAUDIO_ENC_DISPLAY:
        default:
            return dsAUDIO_FORMAT_UNKNOWN;
    }
}

static void dsGetdBRange();

bool dsAudioIsValidHandle(intptr_t uHandle)
{
    for (size_t index = 0; index < dsAUDIOPORT_TYPE_MAX; index++) {
        //hal_info("Checking for a match uHandle: %p and _AOPHandles[index][0].m_nativeHandle: %p\n",
        //        uHandle, _AOPHandles[index][0].m_nativeHandle);
        if ((intptr_t)&_AOPHandles[index][0] == uHandle) {
            hal_info("uHandle: %p is a match\n", uHandle);
            return true;
        }
    }
    return false;
}

static void dsCloseMixerHandle(snd_mixer_t **mixer)
{
    if ((mixer != NULL) && (*mixer != NULL)) {
        snd_mixer_close(*mixer);
        *mixer = NULL;
    }
}

static int8_t initAlsa(const char *selemname, const char *s_card, snd_mixer_t **mixer, snd_mixer_elem_t **element)
{
    hal_info("invoked.\n");
    int ret = 0;
    snd_mixer_t *smixer = NULL;
    snd_mixer_selem_id_t *sid = NULL;

    if ((mixer == NULL) || (element == NULL)) {
        return -1;
    }

    *mixer = NULL;
    *element = NULL;

    if ((ret = snd_mixer_open(&smixer, 0)) < 0) {
        hal_err("Cannot open sound mixer %s\n", snd_strerror(ret));
        return ret;
    }
    if ((ret = snd_mixer_attach(smixer, s_card)) < 0) {
        hal_err("sound mixer attach Failed %s\n", snd_strerror(ret));
        snd_mixer_close(smixer);
        return ret;
    }
    if ((ret = snd_mixer_selem_register(smixer, NULL, NULL)) < 0) {
        hal_err("Cannot register sound mixer element %s\n", snd_strerror(ret));
        snd_mixer_close(smixer);
        return ret;
    }
    ret = snd_mixer_load(smixer);
    if (ret < 0) {
        hal_err("Sound mixer load %s error: %s\n", s_card, snd_strerror(ret));
        snd_mixer_close(smixer);
        return ret;
    }

    ret = snd_mixer_selem_id_malloc(&sid);
    if (ret < 0) {
        hal_err("Sound mixer: id allocation failed. %s: error: %s\n", s_card, snd_strerror(ret));
        snd_mixer_close(smixer);
        return ret;
    }

    snd_mixer_selem_id_set_index(sid, 0);
    snd_mixer_selem_id_set_name(sid, selemname);

    *element = snd_mixer_find_selem(smixer, sid);
    if (NULL == *element) {
        hal_err("Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
        dsCloseMixerHandle(&smixer);
        ret = -1;
    }

    snd_mixer_selem_id_free(sid);

    if (ret == 0) {
        *mixer = smixer;
    }

    return ret;
}

static dsAudioEncoding_t dsEncodingFromIec958Switch(int iec958_enabled, dsAudioEncoding_t cached)
{
    if (!iec958_enabled) {
        return dsAUDIO_ENC_PCM;
    }

    /* IEC958 only tells passthrough ON/OFF; preserve AC3/EAC3 when cached. */
    if (cached == dsAUDIO_ENC_EAC3) {
        return dsAUDIO_ENC_EAC3;
    }
    return dsAUDIO_ENC_AC3;
}

static bool dsEncodingToIec958Switch(dsAudioEncoding_t encoding, int *iec958_enabled)
{
    if (iec958_enabled == NULL) {
        return false;
    }

    switch (encoding) {
        case dsAUDIO_ENC_NONE:
        case dsAUDIO_ENC_PCM:
            *iec958_enabled = 0;
            return true;
        case dsAUDIO_ENC_AC3:
        case dsAUDIO_ENC_EAC3:
            *iec958_enabled = 1;
            return true;
        case dsAUDIO_ENC_DISPLAY:
        default:
            return false;
    }
}

static int dsIec958CtlReadSwitch(const char *s_card, int *iec958_enabled)
{
    if ((s_card == NULL) || (iec958_enabled == NULL)) {
        return -1;
    }

    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_id_t *id = NULL;
    snd_ctl_elem_value_t *value = NULL;
    snd_aes_iec958_t iec958;
    int ret;

    ret = snd_ctl_open(&ctl, s_card, 0);
    if (ret < 0) {
        return ret;
    }

    if ((ret = snd_ctl_elem_id_malloc(&id)) < 0) {
        snd_ctl_close(ctl);
        return ret;
    }

    if ((ret = snd_ctl_elem_value_malloc(&value)) < 0) {
        snd_ctl_elem_id_free(id);
        snd_ctl_close(ctl);
        return ret;
    }

    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_PCM);
    snd_ctl_elem_id_set_name(id, ALSA_IEC958_CTL_NAME);
    snd_ctl_elem_id_set_index(id, 0);
    snd_ctl_elem_id_set_device(id, 0);

    snd_ctl_elem_value_set_id(value, id);
    ret = snd_ctl_elem_read(ctl, value);
    if (ret == 0) {
        snd_ctl_elem_value_get_iec958(value, &iec958);
        *iec958_enabled = ((iec958.status[0] & IEC958_AES0_NONAUDIO) != 0) ? 1 : 0;
    }

    snd_ctl_elem_value_free(value);
    snd_ctl_elem_id_free(id);
    snd_ctl_close(ctl);
    return ret;
}

static int dsIec958CtlWriteSwitch(const char *s_card, int iec958_enabled)
{
    if (s_card == NULL) {
        return -1;
    }

    snd_ctl_t *ctl = NULL;
    snd_ctl_elem_id_t *id = NULL;
    snd_ctl_elem_value_t *value = NULL;
    snd_aes_iec958_t iec958;
    int ret;

    ret = snd_ctl_open(&ctl, s_card, 0);
    if (ret < 0) {
        return ret;
    }

    if ((ret = snd_ctl_elem_id_malloc(&id)) < 0) {
        snd_ctl_close(ctl);
        return ret;
    }

    if ((ret = snd_ctl_elem_value_malloc(&value)) < 0) {
        snd_ctl_elem_id_free(id);
        snd_ctl_close(ctl);
        return ret;
    }

    snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_PCM);
    snd_ctl_elem_id_set_name(id, ALSA_IEC958_CTL_NAME);
    snd_ctl_elem_id_set_index(id, 0);
    snd_ctl_elem_id_set_device(id, 0);

    snd_ctl_elem_value_set_id(value, id);
    ret = snd_ctl_elem_read(ctl, value);
    if (ret == 0) {
        snd_ctl_elem_value_get_iec958(value, &iec958);
        if (iec958_enabled) {
            iec958.status[0] |= IEC958_AES0_NONAUDIO;
        } else {
            iec958.status[0] &= (unsigned char)~IEC958_AES0_NONAUDIO;
        }
        snd_ctl_elem_value_set_iec958(value, &iec958);
        ret = snd_ctl_elem_write(ctl, value);
    }

    snd_ctl_elem_value_free(value);
    snd_ctl_elem_id_free(id);
    snd_ctl_close(ctl);
    return ret;
}

/**
 * @brief Initializes the audio port sub-system of Device Settings HAL.
 *
 * This function initializes all the audio output ports and allocates required resources.
 * It must return dsERR_OPERATION_NOT_SUPPORTED when there are no audio ports present in the device
 * (e.g. a headless gateway device).
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_ALREADY_INITIALIZED      -  Module is already initialised
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 *
 * @warning  This API is Not thread safe.
 *
 * @post dsAudioPortTerm() must be called to release resources.
 *
 * @see dsAudioPortTerm()
 *
 */
dsError_t dsAudioPortInit()
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    if (_bIsAudioInitialized) {
        return dsERR_ALREADY_INITIALIZED;
    }

    _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_vType = dsAUDIOPORT_TYPE_HDMI;
    _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_nativeHandle = dsAUDIOPORT_TYPE_HDMI;
    _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_index = 0;
    _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_IsEnabled = true;

    _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_vType = dsAUDIOPORT_TYPE_SPDIF;
    _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_nativeHandle = dsAUDIOPORT_TYPE_SPDIF;
    _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_index = 0;
    _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_IsEnabled = true;

    hal_info("Audio HDMI Handle: %p\n", (intptr_t)&_AOPHandles[dsAUDIOPORT_TYPE_HDMI][0]);
    hal_info("Audio HDMI m_vType: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_vType);
    hal_info("Audio HDMI m_nativeHandle: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_nativeHandle);
    hal_info("Audio HDMI m_index: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_index);
    hal_info("Audio HDMI m_IsEnabled: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_HDMI][0].m_IsEnabled);

    hal_info("Audio SPDIF Handle: %p\n", (intptr_t)&_AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0]);
    hal_info("Audio SPDIF m_vType: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_vType);
    hal_info("Audio SPDIF m_nativeHandle: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_nativeHandle);
    hal_info("Audio SPDIF m_index: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_index);
    hal_info("Audio SPDIF m_IsEnabled: %d\n", _AOPHandles[dsAUDIOPORT_TYPE_SPDIF][0].m_IsEnabled);

    /* HDMI audio status is now managed via DRM/inotify watcher in display module */
    dsGetdBRange();
    _bIsAudioInitialized = true;
    return ret;
}

static void dsGetdBRange()
{
    hal_info("invoked.\n");
    long min_dB_value, max_dB_value;
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        hal_err("failed to initialize alsa!\n");
        dsCloseMixerHandle(&mixer);
        return;
    }
    if (mixer_elem == NULL) {
        hal_err("initAlsa returned mixer_elem as NULL!\n");
        dsCloseMixerHandle(&mixer);
        return;
    }
    if (!snd_mixer_selem_get_playback_dB_range(mixer_elem, &min_dB_value, &max_dB_value)) {
        dBmax = (float) max_dB_value/100;
        dBmin = (float) min_dB_value/100;
    } else if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        dBmin = 0.0f;
        dBmax = 100.0f;
    } else {
        hal_err("snd_mixer_selem_get_playback_dB_range failed.\n");
    }
    dsCloseMixerHandle(&mixer);
}

/**
 * @brief Gets the audio port handle.
 *
 * This function returns the handle for the type of audio port requested. It must return
 * dsERR_OPERATION_NOT_SUPPORTED if an unavailable audio port is requested.
 *
 * @param[in] type     - Type of audio port (HDMI, SPDIF and so on). Please refer ::dsAudioPortType_t
 * @param[in] index    - Index of audio port depending on the available ports(0, 1, ...). Maximum value of number of ports is platform specific. Please refer ::dsAudioPortConfig_t
 * @param[out] handle  - Pointer to hold the handle of the audio port
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid(port is not present or index is out of range)
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetAudioPort(dsAudioPortType_t type, int index, intptr_t *handle)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == handle || 0 != index || !dsAudioType_isValid(type)) {
        hal_err("Invalid parameters; handle(%p), index(%d) or type(%d).\n", handle, index, type);
        return dsERR_INVALID_PARAM;
    } else {
        *handle = (intptr_t)&_AOPHandles[type][index];
        hal_info("Returning audio Handle: %p\n", *handle);
    }
    return dsERR_NONE;
}

dsError_t dsGetAudioEncoding(intptr_t handle, dsAudioEncoding_t *encoding)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == encoding || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; encoding(%p) or handle(%p).\n", encoding, handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    *encoding = _encoding;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    const char *s_card = dsGetPreferredAlsaCard();
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *iec958_elem = NULL;
    int iec958_enabled = 0;

    /* vc4hdmi0 exposes IEC958 primarily via snd_ctl (matches amixer controls/contents). */
    if (dsIec958CtlReadSwitch(s_card, &iec958_enabled) == 0) {
        *encoding = dsEncodingFromIec958Switch(iec958_enabled, _encoding);
        _encoding = *encoding;
    } else if ((initAlsa("IEC958", s_card, &mixer, &iec958_elem) == 0) && (iec958_elem != NULL) &&
            snd_mixer_selem_has_playback_switch(iec958_elem)) {
        if (snd_mixer_selem_get_playback_switch(iec958_elem, SND_MIXER_SCHN_FRONT_LEFT,
                &iec958_enabled) == 0) {
            *encoding = dsEncodingFromIec958Switch(iec958_enabled, _encoding);
            _encoding = *encoding;
        } else {
            *encoding = _encoding;
            hal_warn("Failed to query IEC958 playback switch; returning cached encoding (%d).\n", *encoding);
        }
    } else {
        *encoding = _encoding;
        hal_warn("IEC958 control not available; returning cached encoding (%d).\n", *encoding);
    }
    dsCloseMixerHandle(&mixer);
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    return dsERR_NONE;
}

/**
 * @brief Gets the audio compression of the specified audio port.
 *
 * This function returns the audio compression level used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle       - Handle for the output audio port
 * @param[out] compression - Pointer to hold the compression value of the specified audio port. (Value ranges from 0 to 10)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioCompression()
 */
dsError_t dsGetAudioCompression(intptr_t handle, int *compression)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == compression || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; compression(%p) or handle(%p).\n", compression, handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    return dsERR_OPERATION_NOT_SUPPORTED;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    /* Compression level is not directly exposed via ALSA; return 0 for PCM and 1 for compressed. */
    dsAudioEncoding_t encoding;
    dsError_t ret = dsGetAudioEncoding(handle, &encoding);
    if (ret != dsERR_NONE) {
        hal_err("dsGetAudioEncoding returned error: %d\n", ret);
        return ret;
    }
    /* There is no platform selected compression level, so return 0 for PCM and 1 for any compressed format. */
    *compression = (encoding == dsAUDIO_ENC_PCM) ? 0 : 1;
    return dsERR_NONE;
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
}

/**
 * @brief Gets the digital audio output mode of digital interfaces.
 *
 * For sink devices, this function returns the digital audio output mode(PCM, Passthrough, DD, DD+) only for the digital interfaces(HDMI ARC/eARC, SPDIF).
 * For source devices, this function returns the digital audio output mode(PCM, Passthrough, DD, DD+, Surround) only for the digital interfaces(HDMI, SPDIF).
 *
 * @param[in] handle      - Handle for the output audio port
 * @param[out] stereoMode - Pointer to hold the stereo mode setting of the
 *                            specified digital interface. Please refer ::dsAudioStereoMode_t
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetStereoMode()
 */
dsError_t dsGetStereoMode(intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == stereoMode || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; stereoMode(%p) or handle(%p).\n", stereoMode, handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    *stereoMode = _stereoModeHDMI;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    const char *s_card = dsGetPreferredAlsaCard();
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    dsAudioEncoding_t encoding = dsAUDIO_ENC_PCM;
    dsError_t ret = dsGetAudioEncoding(handle, &encoding);

    if (ret != dsERR_NONE) {
        hal_err("dsGetAudioEncoding returned error: %d\n", ret);
        return ret;
    }

    if ((initAlsa(element_name, s_card, &mixer, &mixer_elem) == 0) && (mixer_elem != NULL) &&
            snd_mixer_selem_is_playback_mono(mixer_elem)) {
        *stereoMode = dsAUDIO_STEREO_MONO;
        _stereoModeHDMI = *stereoMode;
        hal_info("Audio is Mono; returning %d\n", *stereoMode);
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    switch (encoding) {
        case dsAUDIO_ENC_PCM:
            *stereoMode = dsAUDIO_STEREO_STEREO;
            break;
        case dsAUDIO_ENC_AC3:
            *stereoMode = dsAUDIO_STEREO_DD;
            break;
        case dsAUDIO_ENC_EAC3:
            *stereoMode = dsAUDIO_STEREO_DDPLUS;
            break;
        case dsAUDIO_ENC_DISPLAY:
            *stereoMode = dsAUDIO_STEREO_PASSTHRU;
            break;
        case dsAUDIO_ENC_NONE:
        default:
            *stereoMode = dsAUDIO_STEREO_UNKNOWN;
            break;
    }

    _stereoModeHDMI = *stereoMode;
    hal_info("resolved stereo mode - returning %d\n", *stereoMode);
    dsCloseMixerHandle(&mixer);
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    return dsERR_NONE;
}

dsError_t dsGetPersistedStereoMode(intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
    hal_info("invoked.\n");
    if (NULL == stereoMode || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; stereoMode(%p) or handle(%p).\n", stereoMode, handle);
    }
    return dsERR_NONE;
}

/**
 * @brief Checks if auto mode is enabled or not for the digital interfaces.
 *
 * For sink devices, this function checks whether the digital audio mode auto is enabled for the digital interfaces (HDMI, HDMI ARC/eARC, SPDIF).
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle     - Handle for the output audio port
 * @param[out] autoMode  - Pointer to hold the auto mode setting ( @a true if enabled, @a false if disabled) of the specified digital interface
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetStereoAuto()
 */
dsError_t dsGetStereoAuto(intptr_t handle, int *autoMode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == autoMode || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; autoMode(%p) or handle(%p).\n", autoMode, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device, so this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio mute status of an audio port corresponding to the specified port handle.
 *
 * This function is used to check whether the audio on a specified port is muted or not.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] muted  - Mute status of the specified audio port
 *                        ( @a true if audio is muted, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioMute()
 */
dsError_t dsIsAudioMute(intptr_t handle, bool *muted)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == muted ) {
        hal_err("Invalid parameters; handle(%p) or muted(%p).\n", handle, muted);
        return dsERR_INVALID_PARAM;
    }
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        hal_warn("ALSA mixer not available on HDMI card; mute query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; mute query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (snd_mixer_selem_has_playback_switch(mixer_elem)) {
        int mute_status;
        snd_mixer_selem_get_playback_switch(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &mute_status);
        *muted = !mute_status;
    } else if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        long vol = 0, min = 0, max = 0;
        snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol);
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        (void)max;
        *muted = (vol <= min);
    } else {
        hal_warn("No playback switch on HDMI card; mute query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    dsCloseMixerHandle(&mixer);
    return dsERR_NONE;
}

/**
 * @brief Mutes or un-mutes an audio port.
 *
 * This function mutes or un-mutes the audio port corresponding to the specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] mute    - Flag to mute/un-mute the audio port
 *                        ( @a true to mute, @a false to un-mute)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsAudioMute()
 */
dsError_t dsSetAudioMute(intptr_t handle, bool mute)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameter; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        hal_warn("ALSA mixer not available on HDMI card; mute control unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; mute control unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (snd_mixer_selem_has_playback_switch(mixer_elem)) {
        snd_mixer_selem_set_playback_switch_all(mixer_elem, !mute);
        if (mute) {
            hal_dbg("Audio Mute success\n");
        } else {
            hal_dbg("Audio Unmute success.\n");
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        long min = 0, max = 0, cur = 0;
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &cur);

        if (mute) {
            if (cur > min) {
                _softvolSavedVolume = cur;
            }
            snd_mixer_selem_set_playback_volume_all(mixer_elem, min);
        } else {
            long target = _softvolSavedVolume;
            if (target <= min || target > max) {
                target = min + ((max - min) * 75 / 100);
            }
            snd_mixer_selem_set_playback_volume_all(mixer_elem, target);
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    hal_warn("No playback switch on HDMI card; mute control unsupported.\n");
    dsCloseMixerHandle(&mixer);
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Indicates whether the specified Audio port is enabled or not.
 *
 * @param[in] handle    - Handle of the output audio port
 * @param[out] enabled  - Audio port enabled/disabled state
 *                          ( @a true when audio port is enabled, @a false otherwise)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableAudioPort()
 */
dsError_t dsIsAudioPortEnabled(intptr_t handle, bool *enabled)
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    bool audioEnabled = true;
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (NULL == enabled || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; enabled(%p) or handle(%p).\n", enabled, handle);
        return dsERR_INVALID_PARAM;
    }
    ret = dsIsAudioMute(handle, &audioEnabled);
    if (ret == dsERR_NONE) {
        *enabled = !audioEnabled;
    } else if (ret == dsERR_OPERATION_NOT_SUPPORTED) {
        /* vc4hdmi0 may not expose mixer mute controls; use HAL enabled state. */
        AOPHandle_t *audioPort = (AOPHandle_t *)handle;
        *enabled = audioPort->m_IsEnabled;
        ret = dsERR_NONE;
    } else {
        hal_err("dsIsAudioMute returned error.\n");
    }
    return ret;
}

/**
 * @brief Enables or Disables the Audio port corresponding to the specified port handle.
 *
 * @param[in] handle   - Handle of the output audio port
 * @param[in] enabled  - Flag to control the audio port state
 *                         ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsAudioPortEnabled()
 */
dsError_t dsEnableAudioPort(intptr_t handle, bool enabled)
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    AOPHandle_t *audioPort = (AOPHandle_t *)handle;
    audioPort->m_IsEnabled = enabled;

    ret = dsSetAudioMute(handle, !enabled);
    if (ret == dsERR_OPERATION_NOT_SUPPORTED) {
        /* Keep enable/disable behavior interface-agnostic when mute control is unsupported. */
        return dsERR_NONE;
    }
    return ret;
}

/**
 * @brief Gets the audio gain of an audio port.
 *
 * For sink devices, this function returns the current Dolby DAP Post gain for Speaker(dsAUDIOPORT_TYPE_SPEAKER).
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] gain   - Pointer to hold the audio gain value of the specified audio port
                          The gain ranges between -2080 and 480
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioGain()
 */
dsError_t dsGetAudioGain(intptr_t handle, float *gain)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle) || gain == NULL) {
        hal_err("Invalid parameters; handle(%p) or gain(%p).\n", handle, gain);
        return dsERR_INVALID_PARAM;
    }

    long value_got;
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    long vol_min = 0, vol_max = 0;
    double normalized= 0, min_norm = 0;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        hal_warn("ALSA mixer not available on HDMI card; gain query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; gain query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &vol_min, &vol_max);
        if (!snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &value_got)) {
            *gain = round((float)((value_got - vol_min) * 100.0 / (vol_max - vol_min)));
            dsCloseMixerHandle(&mixer);
            return dsERR_NONE;
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_GENERAL;
    }

    snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max);
    if (!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &value_got))
    {
        hal_info("snd_mixer_selem_get_playback_dB Gain in dB %.2f\n", value_got/100.0);
        if ((vol_max - vol_min) <= MAX_LINEAR_DB_SCALE * 100)
        {
            *gain = (value_got - vol_min) / (double)(vol_max - vol_min);
        }
        else
        {
            normalized = pow(10, (value_got - vol_max) / 6000.0);

            if (vol_min != SND_CTL_TLV_DB_GAIN_MUTE)
            {
                min_norm = pow(10, (vol_min - vol_max) / 6000.0);
                normalized = (normalized - min_norm) / (1 - min_norm);
            }
            *gain = (float)(((int)(100.0f * normalized + 0.5f))/1.0f);
            hal_dbg("Rounded Gain in linear scale %.2f\n", *gain);
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_get_playback_dB error.\n");
    dsCloseMixerHandle(&mixer);
    return dsERR_GENERAL;
}

dsError_t dsGetAudioDB(intptr_t handle, float *db)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle) || db == NULL) {
        hal_err("Invalid parameters; handle(%p) or db(%p).\n", handle, db);
        return dsERR_INVALID_PARAM;
    }

    long db_value;
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;

    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; dB query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        long vol = 0, min = 0, max = 0;
        snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol);
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        *db = (float)((vol - min) * 100.0 / (max - min));
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    if (!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &db_value)) {
        *db = (float) db_value/100;
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_get_playback_dB failed.\n");
    dsCloseMixerHandle(&mixer);
    return dsERR_GENERAL;
}

/**
 * @brief Gets the current audio volume level of an audio port.
 *
 * For sink devices, this function returns the current audio volume level of Speaker(dsAUDIOPORT_TYPE_SPEAKER) and Headphone(dsAUDIOPORT_TYPE_HEADPHONE) ports.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle - Handle for the output audio port
 * @param[out] level - Pointer to hold the audio level value (ranging from 0 to 100) of the specified audio port
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioLevel()
 */
dsError_t dsGetAudioLevel(intptr_t handle, float *level)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == level) {
        hal_err("Invalid parameters; handle(%p) or level(%p).\n", handle, level);
        return dsERR_INVALID_PARAM;
    }

    long vol_value, min, max;
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;

    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;
    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0) {
        hal_warn("ALSA mixer not available on HDMI card; level query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; level query unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }
    if (!snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol_value)) {
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        if (!usingSoftvol && min != vol_value){
            min=min/2;
        }
        *level = round((float)((vol_value - min)*100.0/(max - min)));
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }
    hal_warn("No playback volume control on HDMI card; level query unsupported.\n");
    dsCloseMixerHandle(&mixer);
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetAudioMaxDB(intptr_t handle, float *maxDb)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == maxDb) {
        hal_err("Invalid parameters; handle(%p) or maxDb(%p).\n", handle, maxDb);
        return dsERR_INVALID_PARAM;
    }
    *maxDb = dBmax;
    return dsERR_NONE;
}

dsError_t dsGetAudioMinDB(intptr_t handle, float *minDb)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == minDb) {
        hal_err("Invalid parameters; handle(%p) or minDb(%p).\n", handle, minDb);
        return dsERR_INVALID_PARAM;
    }
    *minDb = dBmin;
    return dsERR_NONE;
}

dsError_t dsGetAudioOptimalLevel(intptr_t handle, float *optimalLevel)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == optimalLevel) {
        hal_err("Invalid parameters; handle(%p) or optimalLevel(%p).\n", handle, optimalLevel);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t  dsIsAudioLoopThru(intptr_t handle, bool *loopThru)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == loopThru) {
        hal_err("Invalid parameters; handle(%p) or loopThru(%p).\n", handle, loopThru);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetAudioEncoding(intptr_t handle, dsAudioEncoding_t encoding)
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    dsAudioFormat_t oldFormat = dsFormatFromEncoding(_encoding);
    dsAudioFormat_t newFormat = dsFormatFromEncoding(encoding);
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    _encoding = encoding;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    const char *s_card = dsGetPreferredAlsaCard();
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *iec958_elem = NULL;
    int iec958_enabled = 0;

    if (!dsEncodingToIec958Switch(encoding, &iec958_enabled)) {
        hal_err("Unsupported encoding(%d) for ALSA experimental path.\n", encoding);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    /* Prefer snd_ctl path for vc4hdmi0 and fall back to mixer where available. */
    if (dsIec958CtlWriteSwitch(s_card, iec958_enabled) != 0) {
        if ((initAlsa("IEC958", s_card, &mixer, &iec958_elem) == 0) && (iec958_elem != NULL) &&
                snd_mixer_selem_has_playback_switch(iec958_elem)) {
            if (snd_mixer_selem_set_playback_switch_all(iec958_elem, iec958_enabled) != 0) {
                hal_err("Failed to set IEC958 playback switch for encoding(%d).\n", encoding);
                dsCloseMixerHandle(&mixer);
                return dsERR_GENERAL;
            }
        } else {
            hal_err("IEC958 control not available; cannot set encoding(%d).\n", encoding);
            dsCloseMixerHandle(&mixer);
            return dsERR_OPERATION_NOT_SUPPORTED;
        }
    }

    _encoding = encoding;
    dsCloseMixerHandle(&mixer);
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */

    if (_halaudioformatCB != NULL && oldFormat != newFormat) {
        _halaudioformatCB(newFormat);
    }
    return ret;
}

/**
 * @brief Sets the audio compression of an audio port.
 *
 * This function sets the audio compression level(non-MS12) to be used on the audio port corresponding to the specified port handle.
 *
 * @param[in] handle       - Handle for the output audio port
 * @param[in] compression  - Audio compression level (value ranges from 0 to 10) to be used on the audio port
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetAudioCompression()
 */
dsError_t dsSetAudioCompression(intptr_t handle, int compression)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || compression < 0 || compression > 10) {
        hal_err("Invalid parameter, handle(%p) or compression(%d).\n", handle, compression);
        return dsERR_INVALID_PARAM;
    }
    // Map compression level to encoding:
    // 0 → PCM (no compression)
    // 1-10 → AC3 (or EAC3, cached preference)
    dsAudioEncoding_t encoding = (compression == 0) ? dsAUDIO_ENC_PCM : dsAUDIO_ENC_AC3;
    return dsSetAudioEncoding(handle, encoding);
}

/**
 * @brief Checks whether the audio port supports Dolby MS11 Multistream Decode.
 *
 * This function checks whether specified audio port supports Dolby MS11 Multistream decode or not.
 *
 * @param[in]  handle         - Handle for the output audio port
 * @param[out] HasMS11Decode  - MS11 Multistream Decode setting for the specified audio port
 *                                ( @a true if audio port supports Dolby MS11 Multistream Decoding or @a false otherwise )
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsIsAudioMSDecode(intptr_t handle, bool *ms11Enabled)
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == ms11Enabled)
    {
        hal_err("Invalid parameters; handle(%p) or ms11Enabled(%p).\n", handle, ms11Enabled);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device; MS11 feature is not supported. */
    *ms11Enabled = _isms11Enabled;
    return ret;
}

/**
 * @brief Sets the digital audio output mode of digital interfaces.
 *
 * For sink devices, this function sets the digital audio output mode(PCM, Passthrough, DD, DD+) to be used only for the digital interfaces(HDMI, HDMI ARC/eARC, SPDIF).
 * For source devices, this function sets the digital audio output mode(PCM, Passthrough, DD, DD+, Surround) to be used only for the digital interfaces(HDMI, SPDIF).
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] mode    - Stereo mode to be used on the specified digital interface. Please refer ::dsAudioStereoMode_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetStereoMode()
 */
dsError_t dsSetStereoMode(intptr_t handle, dsAudioStereoMode_t mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle) || mode >= dsAUDIO_STEREO_MAX || mode <= dsAUDIO_STEREO_UNKNOWN)
    {
        hal_err("Invalid parameters; handle(%p) or mode(%d).\n", handle, mode);
        return dsERR_INVALID_PARAM;
    }

#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    return dsERR_OPERATION_NOT_SUPPORTED;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    dsAudioEncoding_t targetEncoding;
    dsError_t ret = dsERR_NONE;

    /* Map stereo mode to encoding for ALSA control */
    switch (mode) {
        case dsAUDIO_STEREO_STEREO:
            targetEncoding = dsAUDIO_ENC_PCM;
            break;
        case dsAUDIO_STEREO_DD:
            targetEncoding = dsAUDIO_ENC_AC3;
            break;
        case dsAUDIO_STEREO_DDPLUS:
            targetEncoding = dsAUDIO_ENC_EAC3;
            break;
        case dsAUDIO_STEREO_PASSTHRU:
            /* Passthrough: preserve current encoding if AC3/EAC3, else default to AC3 */
            if (_encoding == dsAUDIO_ENC_AC3 || _encoding == dsAUDIO_ENC_EAC3) {
                targetEncoding = _encoding;
            } else {
                targetEncoding = dsAUDIO_ENC_AC3;
            }
            break;
        case dsAUDIO_STEREO_MONO: /* No standard ALSA "set mono" API */
        case dsAUDIO_STEREO_SURROUND:
        case dsAUDIO_STEREO_UNKNOWN:
        default:
            hal_err("Unsupported stereo mode(%d) for ALSA experimental path.\n", mode);
            return dsERR_OPERATION_NOT_SUPPORTED;
    }

    ret = dsSetAudioEncoding(handle, targetEncoding);
    if (ret == dsERR_NONE) {
        _stereoModeHDMI = mode;
        hal_info("Set stereo mode to %d via encoding(%d).\n", mode, targetEncoding);
    }
    return ret;
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
}

/**
 * @brief Sets the Auto Mode to be used on the audio port.
 *
 * For sink devices, this function enables or disables the digital audio mode auto to be used specifically for the digital interfaces(HDMI ARC/eARC, SPDIF).
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle    - Handle for the output audio port
 * @param[in] autoMode  - Indicates the auto mode ( @a true if enabled, @a false if disabled ) to be used on specified digital interface
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetStereoAuto()
 */
dsError_t dsSetStereoAuto(intptr_t handle, int autoMode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || autoMode < 0)
    {
        hal_err("Invalid parameters; handle(%p) or autoMode(%d).\n", handle, autoMode);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device, so this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio gain of an audio port.
 *
 * For sink devices, this function sets the Dolby DAP Post gain to be used for Speaker(dsAUDIOPORT_TYPE_SPEAKER).
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] gain    - Audio Gain to be used on the audio port value
 *                         The Gain ranges between -2080 and 480
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetAudioGain()
 */
dsError_t dsSetAudioGain(intptr_t handle, float gain)
{
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    /* RPi is a source device, so this operation is not supported. */
    hal_dbg("Audio gain control is not supported on source devices.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    bool enabled = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem;

    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0 || mixer_elem == NULL) {
        hal_err("Failed to initialize ALSA!\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_GENERAL;
    }

    if (dsIsAudioMute(handle, &enabled) != dsERR_NONE) {
        hal_err("dsIsAudioMute returned error.\n");
    }
    hal_dbg("Mute status before changing gain: %d\n", enabled);

    long vol_min, vol_max;
    if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        long min = 0, max = 0;
        long vol;
        if (gain < 0.0f) {
            gain = 0.0f;
        }
        if (gain > 100.0f) {
            gain = 100.0f;
        }
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        vol = (long)(((gain / 100.0f) * (max - min)) + min);
        snd_mixer_selem_set_playback_volume_all(mixer_elem, vol);
        if (enabled) {
            hal_dbg("Muting after changing gain to reset to previous state.\n");
            dsError_t muteRet = dsSetAudioMute(handle, enabled);
            dsCloseMixerHandle(&mixer);
            return muteRet;
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    gain /= 100.0f;

    snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max);
    if (vol_max - vol_min <= MAX_LINEAR_DB_SCALE * 100) {
        long floatval = lrint(gain * (vol_max - vol_min)) + vol_min;
        hal_dbg("Setting gain in dB: %.2f \n", floatval / 100.0);
        snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0);
    } else {
        if (vol_min != SND_CTL_TLV_DB_GAIN_MUTE) {
            double min_norm = pow(10, (vol_min - vol_max) / 6000.0);
            gain = gain * (1 - min_norm) + min_norm;
        }
        long floatval = lrint(6000.0 * log10(gain)) + vol_max;
        hal_dbg("Setting gain in dB: %.2f \n", floatval / 100.0);
        snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0);
    }

    if (enabled) {
        hal_dbg("Muting after changing gain to reset to previous state.\n");
        dsError_t muteRet = dsSetAudioMute(handle, enabled);
        dsCloseMixerHandle(&mixer);
        return muteRet;
    }

    dsCloseMixerHandle(&mixer);
    return dsERR_NONE;
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
}

dsError_t dsSetAudioDB(intptr_t handle, float db)
{
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }

    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;

    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0 || mixer_elem == NULL) {
        hal_err("Failed to initialize ALSA!\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_GENERAL;
    }

    db = (db < dBmin) ? dBmin : (db > dBmax) ? dBmax : db;
    hal_info("Setting dB to %.2f\n", db);

    if (usingSoftvol && snd_mixer_selem_has_playback_volume(mixer_elem)) {
        long min = 0, max = 0;
        long vol;
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        vol = (long)(((db / 100.0f) * (max - min)) + min);
        if (snd_mixer_selem_set_playback_volume_all(mixer_elem, vol) == 0) {
            dsCloseMixerHandle(&mixer);
            return dsERR_NONE;
        }
        dsCloseMixerHandle(&mixer);
        return dsERR_GENERAL;
    }

    if (snd_mixer_selem_set_playback_dB_all(mixer_elem, (long) db * 100, 0) == 0) {
        dsCloseMixerHandle(&mixer);
        return dsERR_NONE;
    }

    hal_err("snd_mixer_selem_set_playback_dB_all failed.\n");
    dsCloseMixerHandle(&mixer);
    return dsERR_GENERAL;
}

/**
 * @brief Sets the audio volume level of an audio port.
 *
 * For sink devices, this function sets the audio volume level to be used for Speaker(dsAUDIOPORT_TYPE_SPEAKER) and Headphone(dsAUDIOPORT_TYPE_HEADPHONE) ports.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] level   - Volume level value (ranging from 0 to 100) to be used on the specified audio port
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetAudioLevel()
 */
dsError_t dsSetAudioLevel(intptr_t handle, float level)
{
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle) || level < 0.0 || level > 100.0) {
        hal_err("Invalid parameters; handle(%p) or level(%f).\n", handle, level);
        return dsERR_INVALID_PARAM;
    }

#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    /* RPi is a source device, so this operation is not supported. */
    hal_dbg("Audio level control is not supported on source devices.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    const char *s_card = dsGetPreferredAlsaCard();
    bool usingSoftvol = false;
    snd_mixer_t *mixer = NULL;
    snd_mixer_elem_t *mixer_elem = NULL;

    if (dsInitAudioMixerElem(s_card, &mixer, &mixer_elem, &usingSoftvol) != 0 || mixer_elem == NULL) {
        hal_warn("No simple mixer control on HDMI card; level control unsupported.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_OPERATION_NOT_SUPPORTED;
    }

    long min, max;
    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
    if (!usingSoftvol && level != 0) {
        min /= 2;
    }

    long vol_value = (long)(((level / 100.0) * (max - min)) + min);
    hal_info("Setting volume to %ld\n", vol_value);
    if (snd_mixer_selem_set_playback_volume_all(mixer_elem, vol_value) != 0) {
        hal_err("snd_mixer_selem_set_playback_volume_all failed.\n");
        dsCloseMixerHandle(&mixer);
        return dsERR_GENERAL;
    }

    dsCloseMixerHandle(&mixer);
    return dsERR_NONE;
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
}

dsError_t dsEnableLoopThru(intptr_t handle, bool loopThru)
{
    hal_info("invoked with loopThru %d.\n", loopThru);
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Terminate the Audio Port sub-system of Device Settings HAL.
 *
 * This function terminates all the audio output ports by releasing the audio port specific handles
 * and the allocated resources.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioPortTerm()
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    /* HDMI audio status callbacks removed: now managed by display module */
    pthread_mutex_lock(&gHdmiAudioCbMutex);
    _halhdmiaudioCB = NULL;
    pthread_mutex_unlock(&gHdmiAudioCbMutex);
    _halaudioformatCB = NULL;
    _bIsAudioInitialized = false;
    return ret;
}

bool dsCheckSurroundSupport()
{
    // FIXME: refactor and implement
    hal_info("invoked.\n");
    /* Audio format support detection removed (tvservice eliminated). */
    /* Return false as default since RPI might not support AC3. */
    return false;
}

/**
 * @brief Gets the current audio format.
 *
 * This function returns the audio format of the current playback content(like PCM, DOLBY AC3 etc.) and it is port independent. Please refer ::dsAudioFormat_t
 *
 * @param[in] handle         - Handle for the output audio port
 * @param[out] audioFormat   - Pointer to hold the audio format
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetAudioFormat(intptr_t handle, dsAudioFormat_t *audioFormat)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (audioFormat == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; audioFormat(%p) or handle(%p).\n", audioFormat, handle);
        return dsERR_INVALID_PARAM;
    }
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    return dsERR_OPERATION_NOT_SUPPORTED;
#else /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
    dsAudioEncoding_t encoding;
    dsError_t ret = dsGetAudioEncoding(handle, &encoding);
    if (ret != dsERR_NONE) {
        hal_err("dsGetAudioEncoding returned error: %d\n", ret);
        return ret;
    }

    /* Map encoding to audio format */
    switch (encoding) {
        case dsAUDIO_ENC_PCM:
            *audioFormat = dsAUDIO_FORMAT_PCM;
            break;
        case dsAUDIO_ENC_AC3:
            *audioFormat = dsAUDIO_FORMAT_DOLBY_AC3;
            break;
        case dsAUDIO_ENC_EAC3:
            *audioFormat = dsAUDIO_FORMAT_DOLBY_EAC3;
            break;
        case dsAUDIO_ENC_NONE:
            *audioFormat = dsAUDIO_FORMAT_NONE;
            break;
        case dsAUDIO_ENC_DISPLAY:
        default:
            /* Platform-selected or unknown; cannot determine exact format */
            *audioFormat = dsAUDIO_FORMAT_UNKNOWN;
            break;
    }
    hal_info("Resolved audio format: %d from encoding: %d\n", *audioFormat, encoding);
    return dsERR_NONE;
#endif /* DSHAL_ENABLE_ALSA_EXPERIMENTAL */
}

/**
 * @brief Gets the Dialog Enhancement level of the audio port.
 *
 * This function returns the dialog enhancement level of the audio port corresponding to the specified port handle.
 *
 * @param[in] handle - Handle for the output audio port
 * @param[out] level - Pointer to Dialog Enhancement level (Value ranges from 0 to 12(as per 2.6 IDK) or 0 to 16(as per 2.4.1 IDK))
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetDialogEnhancement()
 */
dsError_t dsGetDialogEnhancement(intptr_t handle, int *level)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (level == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; level(%p) or handle(%p).\n", level, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence dialog enhancement is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Dialog Enhancement level of an audio port.
 *
 * This function sets the dialog enhancement level to be used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port.
 * @param[in] level   - Dialog Enhancement level. Level ranges from 0 to 12(as per 2.6 IDK) or 0 to 16(as per 2.4.1 IDK)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetDialogEnhancement()
 */
dsError_t dsSetDialogEnhancement(intptr_t handle, int level)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || level < 0 || level > 16)
    {
        hal_err("Invalid parameters; handle(%p) or level(%d).\n", handle, level);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence dialog enhancement is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the dolby audio mode status of an audio port.
 *
 * This function returns the dolby audio mode status used in the audio port corresponding to the specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] mode   - Dolby volume mode
 *                        ( @a true if Dolby Volume mode is enabled, and @a false if disabled)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetDolbyVolumeMode()
 */
dsError_t dsGetDolbyVolumeMode(intptr_t handle, bool *mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (mode == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; mode(%p) or handle(%p).\n", mode, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Dolby Volume mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief To enable/disable Dolby Volume Mode.
 *
 * This function sets the dolby audio mode status to the audio port corresponding to port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] mode    - Dolby volume mode.
 *                        ( @a true to enable Dolby volume mode and @a false to disable Dolby volume mode )
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetDolbyVolumeMode()
 */
dsError_t dsSetDolbyVolumeMode(intptr_t handle, bool mode)
{
    hal_info("invoked with mode %d.\n", mode);
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Dolby Volume mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Intelligent Equalizer Mode.
 *
 * This function returns the Intelligent Equalizer Mode setting used in the audio port corresponding to specified Port handle.
 * For source devices, if MS12 DAP Intelligent Equalizer not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle - Handle for the output audio port.
 * @param[out] mode  - Pointer to Intelligent Equalizer mode. 0 = OFF, 1 = Open, 2 = Rich, 3 = Focused,
 *                       4 = Balanced, 5 = Warm, 6 = Detailed
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetIntelligentEqualizerMode()
 */
dsError_t dsGetIntelligentEqualizerMode(intptr_t handle, int *mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (mode == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; mode(%p) or handle(%p).\n", mode, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Intelligent Equalizer mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Intelligent Equalizer Mode.
 *
 * This function sets the Intelligent Equalizer Mode to be used in the audio port corresponding to the specified port handle.
 * For source devices, if MS12 DAP Intelligent Equalizer not supported, then this function retuns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle  - Handle for the output audio port.
 * @param[in] mode    - Intelligent Equalizer mode. 0 = OFF, 1 = Open, 2 = Rich, 3 = Focused,
 *                        4 = Balanced, 5 = Warm, 6 = Detailed
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetIntelligentEqualizerMode()
 */
dsError_t dsSetIntelligentEqualizerMode(intptr_t handle, int mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || mode < 0 || mode > 6)
    {
        hal_err("Invalid parameters; handle(%p) or mode(%d).\n", handle, mode);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Intelligent Equalizer mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Dolby volume leveller settings.
 *
 * This function returns the Volume leveller(mode and level) value used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle       - Handle for the output Audio port
 * @param[out] volLeveller - Pointer to Volume Leveller. Please refer ::dsVolumeLeveller_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetVolumeLeveller()
 */
dsError_t dsGetVolumeLeveller(intptr_t handle, dsVolumeLeveller_t* volLeveller)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if(volLeveller == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; volLeveller(%p) or handle(%p).\n", volLeveller, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Volume Leveller is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Dolby volume leveller settings.
 *
 * This function sets the Volume leveller(mode and level) value to be used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle       - Handle for the output Audio port
 * @param[in] volLeveller  - Volume Leveller setting. Please refer ::dsVolumeLeveller_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetVolumeLeveller()
 */
dsError_t dsSetVolumeLeveller(intptr_t handle, dsVolumeLeveller_t volLeveller)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || volLeveller.mode < 0 ||
            volLeveller.mode > 2 ||
            volLeveller.level < 0 ||
            volLeveller.level > 10 )
    {
        hal_err("Invalid parameters; handle(%p)/volLeveller.mode(%d)/volLeveller.level(%d).\n",
                handle, volLeveller.mode, volLeveller.level);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Volume Leveller is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Bass
 *
 * This function returns the Bass used in a given audio port.
 * For source devices, if MS12 DAP bass enhancer is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[out] boost  - Pointer to Bass Enhancer boost value (ranging from 0 to 100)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetBassEnhancer()
 */
dsError_t dsGetBassEnhancer(intptr_t handle, int *boost)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (boost == NULL || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; boost(%p) or handle(%p).\n", boost, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Bass Enhancer is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio Bass
 *
 * This function sets the Bass to be used in the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP bass enhancer is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[in] boost   - Bass Enhancer boost value (ranging from 0 to 100)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetBassEnhancer()
 */
dsError_t dsSetBassEnhancer(intptr_t handle, int boost)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (boost < 0 || boost > 100 || !dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p) or boost(%d).\n", handle, boost);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Bass Enhancer is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Surround Decoder enabled/disabled status
 *
 * This function returns enable/disable status of surround decoder.
 * For source devices, if MS12 DAP surround decoder is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle   - Handle for the output Audio port
 * @param[out] enabled - Pointer to Surround Decoder enabled(1)/disabled(0) value
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableSurroundDecoder()
 */
dsError_t dsIsSurroundDecoderEnabled(intptr_t handle, bool *enabled)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (enabled == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; enabled(%p) or handle(%p).\n", enabled, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence surround decoder is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables / Disables the audio Surround Decoder.
 *
 * This function will enable/disable surround decoder of the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP surround decoder is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle   - Handle for the output Audio port
 * @param[in] enabled  - Surround Decoder enabled(1)/disabled(0) value
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsIsSurroundDecoderEnabled()
 */
dsError_t dsEnableSurroundDecoder(intptr_t handle, bool enabled)
{
    hal_info("invoked with enabled %d.\n", enabled);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence surround decoder is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the DRC Mode of the specified Audio Port.
 *
 * This function returns the Dynamic Range Control used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle - Handle for the output Audio port
 * @param[out] mode  - Pointer to DRC mode (0 for DRC line mode and 1 for DRC RF mode)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetDRCMode()
 */
dsError_t dsGetDRCMode(intptr_t handle, int *mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (mode == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; mode(%p) or handle(%p).\n", mode, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence DRC mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the DRC Mode of specified audio port.
 *
 * This function sets the Dynamic Range Control to be used in the audio port corresponding to port handle.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[in] mode    - DRC mode (0 for DRC Line Mode and 1 for DRC RF mode)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetDRCMode()
 */
dsError_t dsSetDRCMode(intptr_t handle, int mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (mode < 0 || mode > 1 || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p) or mode(%d).\n", handle, mode);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence DRC mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Surround Virtualizer level.
 *
 * This function returns the Surround Virtualizer level(mode and boost) used in the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP surround virtualizer is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle       - Handle for the output Audio port
 * @param[out] virtualizer - Surround virtualizer setting. Please refer ::dsSurroundVirtualizer_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetSurroundVirtualizer()
 */
dsError_t dsGetSurroundVirtualizer(intptr_t handle, dsSurroundVirtualizer_t *virtualizer)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (virtualizer == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; virtualizer(%p) or handle(%p).\n", virtualizer, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Surround Virtualizer is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio Surround Virtualizer level
 *
 * This function sets the Surround Virtualizer level(mode and boost) to be used in the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP surround virtualizer is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle       - Handle for the output Audio port
 * @param[in] virtualizer  - Surround virtualizer setting. Please refer ::dsSurroundVirtualizer_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetSurroundVirtualizer()
 */
dsError_t dsSetSurroundVirtualizer(intptr_t handle, dsSurroundVirtualizer_t virtualizer)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    if (virtualizer.mode < 0 || virtualizer.mode > 2) {
        hal_err("Invalid mode; mode(%d).\n", virtualizer.mode);
        return dsERR_INVALID_PARAM;
    }
    if (virtualizer.boost < 0 || virtualizer.boost > 96) {
        hal_err("Invalid boost; boost(%d).\n", virtualizer.boost);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Surround Virtualizer is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Media Intelligent Steering of the audio port.
 *
 * For sink devices, this function returns enable/disable status of Media Intelligent Steering for the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP Media Intelligent Steering is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle    - Handle for the output Audio port
 * @param[out] enabled  - MI Steering enabled(1)/disabled(0) value
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetMISteering()
 */
dsError_t dsGetMISteering(intptr_t handle, bool *enabled)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (enabled == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; enabled(%p) or handle(%p).\n", enabled, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Media Intelligent Steering is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Set the Media Intelligent Steering of the audio port.
 *
 * For sink devices, this function sets the enable/disable status of Media Intelligent Steering for the audio port corresponding to specified port handle.
 * For source devices, if MS12 DAP Media Intelligent Steering is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle   - Handle for the output Audio port
 * @param[in] enabled  - MI Steering enabled(1)/disabled(0) value
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetMISteering()
 */
dsError_t dsSetMISteering(intptr_t handle, bool enabled)
{
    hal_info("invoked with enabled %d.\n", enabled);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Media Intelligent Steering is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Graphic Equalizer Mode.
 *
 * For sink devices, this function returns the Graphic Equalizer Mode setting used in the audio port corresponding to the specified port handle.
 * For source devices, if MS12 DAP Graphic Equalizer Mode is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle - Handle for the output audio port.
 * @param[out] mode  - Graphic Equalizer Mode. 0 = EQ OFF, 1 = EQ Open, 2 = EQ Rich and 3 = EQ Focused
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetGraphicEqualizerMode()
 */
dsError_t dsGetGraphicEqualizerMode(intptr_t handle, int *mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (mode == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; mode(%p) or handle(%p).\n", mode, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Graphic Equalizer mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Graphic Equalizer Mode.
 *
 * For sink devices, this function sets the Graphic Equalizer Mode setting to be used in the audio port corresponding to the specified port handle.
 * For source devices, if MS12 DAP Graphic Equalizer Mode is not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle  - Handle for the output audio port.
 * @param[in] mode    - Graphic Equalizer mode. 0 for EQ OFF, 1 for EQ Open, 2 for EQ Rich and 3 for EQ Focused
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetGraphicEqualizerMode()
 */
dsError_t dsSetGraphicEqualizerMode(intptr_t handle, int mode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || mode < 0 || mode > 3) {
        hal_err("Invalid parameters; handle(%p) or mode(%d).\n", handle, mode);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 DAP, hence Graphic Equalizer mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported MS12 audio profiles
 *
 * For sink devices, this function will get the list of supported MS12 audio profiles.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle     - Handle for the output Audio port
 * @param[out] profiles  - List of supported audio profiles. Please refer ::dsMS12AudioProfileList_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetMS12AudioProfile()
 */
dsError_t dsGetMS12AudioProfileList(intptr_t handle, dsMS12AudioProfileList_t *profiles)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (profiles == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; profiles(%p) or handle(%p).\n", profiles, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device, hence MS12 audio profiles are not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets current audio profile selection
 *
 * For sink devices, this function gets the current audio profile configured.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle    - Handle for the output Audio port
 * @param[out] profile  - Audio profile configured currently
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetMS12AudioProfile()
 */
dsError_t dsGetMS12AudioProfile(intptr_t handle, char *profile)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (profile == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; profile(%p) or handle(%p).\n", profile, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device, hence MS12 audio profiles are not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported ARC types of the connected ARC/eARC device
 *
 * For sink devices, this function gets the supported ARC types of the connected device on ARC/eARC port.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle - Handle for the HDMI ARC/eARC port
 * @param[out] types - Value of supported ARC types. Please refer ::dsAudioARCTypes_t
 *
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetSupportedARCTypes(intptr_t handle, int *types)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (types == NULL|| !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; types(%p) or handle(%p).\n", types, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi is a source device, hence ARC types are not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets Short Audio Descriptor retrieved from CEC for the connected ARC device
 *
 * For sink devices, this function sets the Short Audio Descriptor based on best available options
 * of Audio capabilities supported by connected ARC device. Required when ARC output
 * mode is Auto/Passthrough. Please refer ::dsAudioSADList_t, ::dsSetStereoMode.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle   - Handle for the HDMI ARC/eARC port.
 * @param[in] sad_list - All SADs retrieved from CEC for the connected ARC device.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioSetSAD(intptr_t handle, dsAudioSADList_t sad_list)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p) with SAD.count %d.\n", handle, sad_list.count);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enable/Disable ARC/eARC and route audio to connected device.
 *
 * For sink devices, this function enables/disables ARC/eARC and routes audio to connected device. Please refer ::_dsAudioARCStatus_t and ::dsAudioARCTypes_t .
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle    - Handle for the HDMI ARC/eARC port
 * @param[in] arcStatus - ARC/eARC feature. Please refer ::_dsAudioARCStatus_t
 *                          ( @a true to enable ARC/eARC, @a false to disable )
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioEnableARC(intptr_t handle, dsAudioARCStatus_t arcStatus)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p) with ARC.status %d.\n", handle, arcStatus.status);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio delay (in ms) of an audio port
 *
 * For sink devices, this function returns the digital audio delay (in milliseconds) of the digital interfaces(HDMI ARC/eARC, SPDIF).
 * The Audio delay ranges from 0 to 200 milliseconds.
 * For source devices, this function returns the digital audio delay (in milliseconds) of the digital interfaces(HDMI, SPDIF)
 *
 * @param[in] handle        - Handle for the output Audio port
 * @param[out] audioDelayMs - Pointer to Audio delay
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioDelay()
 */
dsError_t dsGetAudioDelay(intptr_t handle, uint32_t *audioDelayMs)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (audioDelayMs == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; audioDelayMs(%p) or handle(%p).\n", audioDelayMs, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi vc4-hdmi driver does not expose a configurable audio delay via ALSA or sysfs. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio delay (in ms) of an audio port.
 *
 * For sink devices, this function will set the audio delay (in milliseconds) of the digital interfaces(HDMI ARC/eARC, SPDIF).
 * The Audio delay ranges from 0 to 200 milliseconds.
 * For source devices, this function will set the audio delay (in milliseconds) of the digital interfaces(HDMI, SPDIF).
 *
 * @param[in] handle        - Handle for the output Audio port
 * @param[in] audioDelayMs  - Amount of delay(in milliseconds)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetAudioDelay()
 */
dsError_t dsSetAudioDelay(intptr_t handle, const uint32_t audioDelayMs)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || audioDelayMs > 200) {
        hal_err("Invalid parameters; handle(%p) or audioDelayMs(%d).\n", handle, audioDelayMs);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetAudioDelayOffset(intptr_t handle, uint32_t *audioDelayOffsetMs)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (audioDelayOffsetMs == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; audioDelayOffsetMs(%p) or handle(%p).\n", audioDelayOffsetMs, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi vc4-hdmi driver does not expose a configurable audio delay offset via ALSA or sysfs. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetAudioDelayOffset(intptr_t handle, const uint32_t audioDelayOffsetMs)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p) with audioDelayOffsetMs %d.\n", handle, audioDelayOffsetMs);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio ATMOS output mode.
 *
 * This function will set the dolby atmos lock provided by MS12 and it is port independent.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[in] enable  - Audio ATMOS output mode( @a true to enable  @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsSetAudioAtmosOutputMode(intptr_t handle, bool enable)
{
    hal_info("invoked with enable %d.\n", enable);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Dolby Atmos output mode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the ATMOS capability of the sink device.
 *
 * This function returns the ATMOS capability of the sink device.
 *
 * @param[in] handle       - Handle for the output Audio port
 * @param[out] capability  - ATMOS capability of sink device. Please refer ::dsATMOSCapability_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetSinkDeviceAtmosCapability(intptr_t handle, dsATMOSCapability_t *capability)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (capability == NULL || !dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; capability(%p) or handle(%p).\n", capability, handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence ATMOS capability is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @note This API is deprecated.
 *
 * @brief Enables or Disables MS12 DAPV2 and DE feature
 *
 * For sink and source devices,this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle   - Handle of the output audio port
 * @param[in] feature  - Enums for MS12 features. Please refer ::dsMS12FEATURE_t
 * @param[in] enable   - Flag to control the MS12 features
 *                         ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetMS12AudioProfileList(), dsGetMS12AudioProfile()
 */
dsError_t dsEnableMS12Config(intptr_t handle, dsMS12FEATURE_t feature, const bool enable)
{
    hal_info("invoked with enable %d.\n", enable);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || feature < dsMS12FEATURE_DAPV2 || feature >= dsMS12FEATURE_MAX) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence MS12 features are not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables or Disables Loudness Equivalence feature.
 *
 * For source devices,if LE not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle  - Handle of the output audio port
 * @param[in] enable  - Flag to control the LE features
 *                        ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetLEConfig()
 */
dsError_t dsEnableLEConfig(intptr_t handle, const bool enable)
{
    hal_info("invoked with enable %d.\n", enable);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support LE, hence LE features are not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the LE (Loudness Equivalence) configuration.
 *
 * This function is used to get LE (Loudness Equivalence) feature of the audio port corresponding to specified port handle.
 * For source devices, if LE not supported, then this function returns dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @param[in] handle   - Handle for the output Audio port
 * @param[out] enable  - Flag which return status of LE features
 *                         ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsEnableLEConfig()
 */
dsError_t dsGetLEConfig(intptr_t handle, bool *enable)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || enable == NULL) {
        hal_err("Invalid parameters; handle(%p) or enable(%p).\n", handle, enable);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence Loudness Equivalence (LE) is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the MS12 audio profile
 *
 * For sink devices, this function will configure the user selected ms12 audio profile.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle   - Handle for the output audio port
 * @param[in] profile  - Audio profile to be used from the supported list. Please refer ::_dsMS12AudioProfileList_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetMS12AudioProfile(), dsGetMS12AudioProfileList()
 */
dsError_t dsSetMS12AudioProfile(intptr_t handle, const char* profile)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || profile == NULL) {
        hal_err("Invalid parameters; handle(%p) or profile(%p).\n", handle, profile);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence setting MS12 audio profile is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetAudioDucking(intptr_t handle, dsAudioDuckingAction_t action, dsAudioDuckingType_t type, const unsigned char level)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || level > 100 || (action < dsAUDIO_DUCKINGACTION_START)
            || (action >= dsAudio_DUCKINGACTION_MAX) || (type < dsAUDIO_DUCKINGTYPE_ABSOLUTE)
            || (type >= dsAudio_DUCKINGTYPE_MAX)) {
        hal_err("Invalid parameters; handle(%p) or level(%d) or action(%d) or type(%d).\n", handle, level, action, type);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Checks whether the audio port supports Dolby MS12 Multistream Decode.
 *
 * This function checks whether specified audio port supports Dolby MS12 Multistream decode or not.
 *
 * @param[in] handle          - Handle for the output audio port
 * @param[out] hasMS12Decode  - MS12 Multistream Decode setting
 *                                ( @a true if audio port supports Dolby MS12 Multistream Decoding or @a false otherwise )
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 */
dsError_t dsIsAudioMS12Decode(intptr_t handle, bool *hasMS12Decode)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || hasMS12Decode == NULL) {
        hal_err("Invalid parameters; handle(%p) or hasMS12Decode(%p).\n", handle, hasMS12Decode);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12, hence checking MS12 audio decode is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Checks if the audio output port is connected or not.
 *
 * For sink devices, this function is used to check if the headphone port is connected or not.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] handle        - Handle for the output Audio port
 * @param[out] isConnected  - Flag for audio port connection status
 *                              ( @a true if audio port is connected and @a false if Not Connected)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioOutIsConnected(intptr_t handle, bool* isConnected)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || isConnected == NULL) {
        hal_err("Invalid parameters; handle(%p) or isConnected(%p).\n", handle, isConnected);
        return dsERR_INVALID_PARAM;
    }
    for (int i = 0; i < dsAUDIOPORT_TYPE_MAX; i++) {
        for (int j = 0; j < 2; j++) {
            //hal_info("Checking handle (%p) against &_AOPHandles[i][j](%p)\n", handle, (intptr_t)&_AOPHandles[i][j]);
            if ((intptr_t)&_AOPHandles[i][j] == handle) {
                *isConnected = _AOPHandles[i][j].m_IsEnabled;
                hal_info("Found handle (%p) at index (%d, %d) with connection %d\n", handle, i, j, *isConnected);
                return dsERR_NONE;
            }
        }
    }
    hal_err("Invalid handle (%p).\n", handle);
    return dsERR_GENERAL;
}

/**
 * @brief Registers for the Audio Output Port Connect Event
 *
 * For sink devices, this function is used to register for Audio Output Connect Event. This callback is Headphone specific
 * and will be triggered whenever there is a change in Headphone connection status change.
 * For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
 *
 * @param[in] CBFunc  - Audio output port connect callback function.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioOutRegisterConnectCB(dsAudioOutPortConnectCB_t CBFunc)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (CBFunc == NULL) {
        hal_err("Invalid parameters; CBFunc(%p).\n", CBFunc);
        return dsERR_INVALID_PARAM;
    }
    pthread_mutex_lock(&gHdmiAudioCbMutex);
    if (NULL != _halhdmiaudioCB) {
        hal_warn("CBFunc already registered; overriding with new callback handle.\n");
    }
    _halhdmiaudioCB = CBFunc;
    pthread_mutex_unlock(&gHdmiAudioCbMutex);
    return dsERR_NONE;
}

/**
 * @brief Registers for the Audio Format Update Event
 *
 * This function is used to register for the Audio Format Update Event
 *
 * @param[in] cbFun  - Audio format update callback function.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsAudioFormatUpdateRegisterCB(dsAudioFormatUpdateCB_t cbFun)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (cbFun == NULL) {
        hal_err("Invalid parameters; cbFun(%p).\n", cbFun);
        return dsERR_INVALID_PARAM;
    }
    if (NULL != _halaudioformatCB) {
        hal_warn("cbFun already registered; overriding with new callback handle.\n");
    }
    _halaudioformatCB = cbFun;
    return dsERR_NONE;
}

/**
 * @brief Register for the Atmos capability change event of the sink device
 *
 * @param[in] cbFun  - Atmos Capability chance callback function.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
**/
dsError_t dsAudioAtmosCapsChangeRegisterCB(dsAtmosCapsChangeCB_t cbFun)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (cbFun == NULL) {
        hal_err("Invalid parameters; cbFun(%p).\n", cbFun);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Audio Format capabilities.
 *
 * This function is used to get the supported Audio capabilities of the platform.
 *
 * @param[in]  handle        - Handle for the output audio port
 * @param[out] capabilities  - Bitwise OR value of supported Audio standards. Please refer ::dsAudioCapabilities_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetAudioCapabilities(intptr_t handle, int *capabilities)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || capabilities == NULL) {
        hal_err("Invalid parameters; handle(%p) or capabilities(%p).\n", handle, capabilities);
        return dsERR_INVALID_PARAM;
    }
    /* RPi supports PCM always; DD and DD+ are available via IEC958 passthrough when ALSA experimental is enabled. */
#ifndef DSHAL_ENABLE_ALSA_EXPERIMENTAL
    *capabilities = dsAUDIOSUPPORT_NONE;
#else
    /* RPi vc4-hdmi driver supports passthrough of DD & DD+ when ALSA experimental features are enabled. */
    *capabilities = dsAUDIOSUPPORT_DD | dsAUDIOSUPPORT_DDPLUS;
#endif
    return dsERR_NONE;
}

/**
 * @brief Gets the MS12 capabilities supported by the platform.
 *
 * This function is used to get the supported MS12 capabilities of the platform and it is port independent.
 *
 * @param[in]  handle        - Handle for the output audio port
 * @param[out] capabilities  - OR-ed value of supported MS12 standards. Please refer ::dsMS12Capabilities_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 */
dsError_t dsGetMS12Capabilities(intptr_t handle, int *capabilities)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || capabilities == NULL) {
        hal_err("Invalid parameters; handle(%p) or capabilities(%p).\n", handle, capabilities);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 features; report explicit NONE capability. */
    *capabilities = dsMS12SUPPORT_NONE;
    return dsERR_NONE;
}

dsError_t dsResetDialogEnhancement(intptr_t handle)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsResetBassEnhancer(intptr_t handle)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsResetSurroundVirtualizer(intptr_t handle)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsResetVolumeLeveller(intptr_t handle)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables/Disables associated audio mixing feature.
 *
 * This function will enable/disable associated audio mixing feature of playback content and it is port independent.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] mixing  - Flag to control audio mixing feature
 *                        ( @a true to enable, @a false to disable)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_OPERATION_FAILED         -  The attempted operation failed
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetAssociatedAudioMixing()
 */
dsError_t dsSetAssociatedAudioMixing(intptr_t handle, bool mixing)
{
    hal_info("invoked with mixing %d.\n", mixing);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 associated audio mixing, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Associated Audio Mixing status - enabled/disabled
 *
 * This function is used to get the audio mixing status(enabled/disabled) of playback content and it is port independent.
 *
 * @param[in] handle   - Handle for the output Audio port
 * @param[out] mixing  - Associated Audio Mixing status
 *                         ( @a true if enabled and @a false if disabled)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAssociatedAudioMixing()
 */
dsError_t dsGetAssociatedAudioMixing(intptr_t handle, bool *mixing)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || mixing == NULL) {
        hal_err("Invalid parameters; handle(%p) or mixing(%p).\n", handle, mixing);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 associated audio mixing, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the mixerbalance between main and associated audio
 *
 * This function will set the mixerbalance between main and associated audio of audio port corresponding to specified port handle.
 *
 * @param[in] handle        - Handle for the output Audio port
 * @param[in] mixerbalance  - int value -32(mute associated audio) to +32(mute main audio)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetFaderControl()
 */
dsError_t dsSetFaderControl(intptr_t handle, int mixerbalance)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) ||  mixerbalance < -32 || mixerbalance > 32) {
        hal_err("Invalid parameters; handle(%p) or mixerbalance(%d).\n", handle, mixerbalance);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 fader control, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief To get the mixer balance between main and associated audio
 *
 * This function will get the mixer balance between main and associated audio of audio port corresponding to specified port handle.
 *
 * @param[in]  handle        - Handle for the output Audio port
 * @param[out] mixerbalance  - int value -32(mute associated audio) to +32(mute main audio)
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetFaderControl()
 */
dsError_t dsGetFaderControl(intptr_t handle, int* mixerbalance)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) ||  mixerbalance == NULL) {
        hal_err("Invalid parameters; handle(%p) or mixerbalance(%p).\n", handle, mixerbalance);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support MS12 fader control, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets AC4 Primary language
 *
 * This function will set AC4 Primary language of the playback content and it is port independent.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[in] pLang   - char* 3 letter language code string as per ISO 639-3
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetPrimaryLanguage()
 */
dsError_t dsSetPrimaryLanguage(intptr_t handle, const char* pLang)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) ||  pLang == NULL) {
        hal_err("Invalid parameters; handle(%p) or pLang(%p).\n", handle, pLang);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support AC4 language selection controls, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief To get AC4 Primary language
 *
 * This function will get AC4 Primary language of the playback content and it is port independent.
 *
 * @param[in] handle  - Handle for the output Audio port
 * @param[out] pLang  - char* 3 letter lang code should be used as per ISO 639-3
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetPrimaryLanguage()
 */
dsError_t dsGetPrimaryLanguage(intptr_t handle, char* pLang)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || pLang == NULL) {
        hal_err("Invalid parameters; handle(%p) or pLang(%p).\n", handle, pLang);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support AC4 language selection controls, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief To set AC4 Secondary language
 *
 * This function will set AC4 Secondary language of the playback content and it is port independent.
 * Since language selection is preference-based, the primary language takes the highest priority.
 * If the primary language is not set or its corresponding audio track is unavailable, playback will
 * default to the secondary language configuration if set.
 *
 * @param[in] handle  - Handle for the output Audio port (Not Used as setting is not port specific)
 * @param[in] sLang   - char* 3 letter lang code should be used as per ISO 639-3
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsGetSecondaryLanguage()
 */
dsError_t dsSetSecondaryLanguage(intptr_t handle, const char* sLang)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) ||  sLang == NULL) {
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support AC4 language selection controls, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the AC4 Secondary language
 *
 * This function will get AC4 Secondary language of the playback content and it is port independent.
 *
 * @param[in] handle  - Handle for the output Audio port (Not Used as setting is not port specific)
 * @param[out] sLang  - char* 3 letter lang code should be used as per ISO 639-3
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetSecondaryLanguage()
 */
dsError_t dsGetSecondaryLanguage(intptr_t handle, char* sLang)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || sLang == NULL) {
        hal_err("Invalid parameters; handle(%p) or sLang(%p).\n", handle, sLang);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support AC4 language selection controls, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsGetHDMIARCPortId(int *portId)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (portId == NULL) {
        hal_err("Invalid parameters; portId(%p).\n", portId);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support HDMI ARC port ID retrieval, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
* @brief Sets the Mixer Volume level of sink device for the given input
* This API is specific to sink devices
*
* For sink devices, this function sets the mixer volume level for either primary(main audio) or system audio input(System Beep) and it is port independent.
* For source devices, this function returns dsERR_OPERATION_NOT_SUPPORTED always.
*
* @param[in] handle  - A valid handle refers to a specific audio port handle on the platform, or a NULL handle refers to use the current active port
* @param[in] aInput  - dsAudioInputPrimary / dsAudioInputSystem. Please refer ::dsAudioInput_t
* @param[in] volume  - volume to be set (0 to 100)
*
* @return dsError_t                        - Status
* @retval dsERR_NONE                       - Success
* @retval dsERR_NOT_INITIALIZED            - Module is not initialised
* @retval dsERR_INVALID_PARAM              - Parameter passed to this function is invalid
* @retval dsERR_OPERATION_NOT_SUPPORTED    - The attempted operation is not supported; e.g: source devices
* @retval dsERR_GENERAL                    - Underlying undefined platform error
*
* @pre dsAudioPortInit() should be called before calling this API
*      dsGetAudioPort() should be called if a valid handle is used other than NULL
*
*/
dsError_t dsSetAudioMixerLevels(intptr_t handle, dsAudioInput_t aInput, int volume)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || volume < 0 || volume > 100
            || aInput < dsAUDIO_INPUT_PRIMARY  || aInput >= dsAUDIO_INPUT_MAX)
    {
        hal_err("Invalid parameters; handle(%p) or volume(%d) or aInput(%d).\n", handle, volume, aInput);
        return dsERR_INVALID_PARAM;
    }
    /* RPi does not support audio mixer level controls, hence this operation is not supported. */
    return dsERR_OPERATION_NOT_SUPPORTED;
}
