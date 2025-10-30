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
#include "dsError.h"
#include "dsUtl.h"
#include "dshalUtils.h"
#include <alsa/asoundlib.h>
#include "dshalLogger.h"

#define ALSA_CARD_NAME "hw:0"
#if (SND_LIB_MAJOR >= 1) && (SND_LIB_MINOR >= 2) && (KERNEL_ARPI_VERSION_MAJOR < 6)
#define ALSA_ELEMENT_NAME "HDMI"
#else
#define ALSA_ELEMENT_NAME "PCM"
#endif

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

dsAudioOutPortConnectCB_t _halhdmiaudioCB = NULL;
static void tvservice_hdmiaudio_callback(void *callback_data, uint32_t reason, uint32_t param1, uint32_t param2)
{
    AOPHandle_t *aopHandle = (AOPHandle_t *)callback_data;
    bool invokeCallback = false;
    hal_info("Got event reason: %d, param1: %d, param2: %d\n", reason, param1, param2);
    if (reason == VC_HDMI_UNPLUGGED) {
        hal_info("HDMI disconnected\n");
        aopHandle->m_IsEnabled = true;
        invokeCallback = true;
    } else if (reason == VC_HDMI_ATTACHED) {
        hal_info("HDMI connected\n");
        aopHandle->m_IsEnabled = false;
        invokeCallback = true;
    }
    if (NULL != _halhdmiaudioCB && invokeCallback) {
        // TODO: verify param2(0) is correct or not.
        // uiPortNo  - Port number in which the connection status changed.
        _halhdmiaudioCB(dsAUDIOPORT_TYPE_HDMI, 0, aopHandle->m_IsEnabled);
    } else {
        hal_warn("hdmi_audio_cb is NULL, dropping event triggers.\n");
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

static int8_t initAlsa(const char *selemname, const char *s_card, snd_mixer_elem_t **element)
{
    hal_info("invoked.\n");
    int ret = 0;
    snd_mixer_t *smixer = NULL;
    snd_mixer_selem_id_t *sid;

    if ((ret = snd_mixer_open(&smixer, 0)) < 0) {
        hal_err("Cannot open sound mixer %s\n", snd_strerror(ret));
        snd_mixer_close(smixer);
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
        snd_mixer_close(smixer);
        ret = -1;
    }

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

    /* Add listener for HDMI status changes - for connected audio status update. */
    vc_tv_register_callback(&tvservice_hdmiaudio_callback, &_AOPHandles[dsAUDIOPORT_TYPE_HDMI][0]);

    dsGetdBRange();
    _bIsAudioInitialized = true;
    return ret;
}

static void dsGetdBRange()
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    long min_dB_value, max_dB_value;
    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_elem_t *mixer_elem = NULL;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
        return;
    }
    if (!snd_mixer_selem_get_playback_dB_range(mixer_elem, &min_dB_value, &max_dB_value)) {
        dBmax = (float) max_dB_value/100;
        dBmin = (float) min_dB_value/100;
    } else {
        hal_err("snd_mixer_selem_get_playback_dB_range failed.\n");
    }
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

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
    *encoding = _encoding;
    return dsERR_NONE;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    *stereoMode = _stereoModeHDMI;
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

dsError_t dsGetStereoAuto (intptr_t handle, int *autoMode)
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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsIsAudioMute(intptr_t handle, bool *muted)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || NULL == muted ) {
        hal_err("Invalid parameters; handle(%p) or muted(%p).\n", handle, muted);
        return dsERR_INVALID_PARAM;
    }
    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_elem_t *mixer_elem = NULL;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
        return dsERR_GENERAL;
    }
    int mute_status;
    if (snd_mixer_selem_has_playback_switch(mixer_elem)) {
        snd_mixer_selem_get_playback_switch(mixer_elem,  SND_MIXER_SCHN_FRONT_LEFT, &mute_status);
        if (!mute_status) {
            *muted = true;
        } else {
            *muted = false;
        }
    } else {
        hal_err("snd_mixer_selem_has_playback_switch failed\n");
        return dsERR_GENERAL;
    }
    return dsERR_NONE;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

dsError_t dsSetAudioMute(intptr_t handle, bool mute)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameter; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_elem_t *mixer_elem = NULL;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
        return dsERR_GENERAL;
    }
    if (snd_mixer_selem_has_playback_switch(mixer_elem)) {
        snd_mixer_selem_set_playback_switch_all(mixer_elem, !mute);
        if (mute) {
            hal_dbg("Audio Mute success\n");
        } else {
            hal_dbg("Audio Unmute success.\n");
        }
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_has_playback_switch failed\n");
    return dsERR_GENERAL;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

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
    } else {
        hal_err("dsIsAudioMute returned error.\n");
    }
    return ret;
}

dsError_t dsEnableAudioPort(intptr_t handle, bool enabled)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsSetAudioMute(handle, !enabled);
}

dsError_t dsGetAudioGain(intptr_t handle, float *gain)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
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
    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    long vol_min = 0, vol_max = 0;
    double normalized= 0, min_norm = 0;
    snd_mixer_elem_t *mixer_elem;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
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
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_get_playback_dB error.\n");
    return dsERR_GENERAL;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

dsError_t dsGetAudioDB(intptr_t handle, float *db)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
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
    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;

    snd_mixer_elem_t *mixer_elem = NULL;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
        return dsERR_GENERAL;
    }

    if (!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &db_value)) {
        *db = (float) db_value/100;
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_get_playback_dB failed.\n");
    return dsERR_GENERAL;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

dsError_t dsGetAudioLevel(intptr_t handle, float *level)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
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
    const char *s_card = "default";
    const char *element_name = ALSA_ELEMENT_NAME;

    snd_mixer_elem_t *mixer_elem = NULL;
    initAlsa(element_name,s_card,&mixer_elem);
    if (mixer_elem == NULL) {
        hal_err("failed to initialize alsa!\n");
        return dsERR_GENERAL;
    }
    if (!snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol_value)) {
        snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
        if (min != vol_value){
            min=min/2;
        }
        *level = round((float)((vol_value - min)*100.0/(max - min)));
        return dsERR_NONE;
    }
    hal_err("snd_mixer_selem_get_playback_volume failed.\n");
    return dsERR_GENERAL;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
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
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle))
    {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    _encoding = encoding;
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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    *ms11Enabled = _isms11Enabled;
    return ret;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetStereoAuto (intptr_t handle, int autoMode)
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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetAudioGain(intptr_t handle, float gain)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }

    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    bool enabled = false;
    snd_mixer_elem_t *mixer_elem;

    if (initAlsa(element_name, s_card, &mixer_elem) != 0 || mixer_elem == NULL) {
        hal_err("Failed to initialize ALSA!\n");
        return dsERR_GENERAL;
    }

    if (dsIsAudioMute(handle, &enabled) != dsERR_NONE) {
        hal_err("dsIsAudioMute returned error.\n");
    }
    hal_dbg("Mute status before changing gain: %d\n", enabled);

    long vol_min, vol_max;
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
        return dsSetAudioMute(handle, enabled);
    }

    return dsERR_NONE;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

dsError_t dsSetAudioDB(intptr_t handle, float db)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle)) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }

    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_elem_t *mixer_elem = NULL;

    if (initAlsa(element_name, s_card, &mixer_elem) != 0 || mixer_elem == NULL) {
        hal_err("Failed to initialize ALSA!\n");
        return dsERR_GENERAL;
    }

    db = (db < dBmin) ? dBmin : (db > dBmax) ? dBmax : db;
    hal_info("Setting dB to %.2f\n", db);
    if (snd_mixer_selem_set_playback_dB_all(mixer_elem, (long) db * 100, 0) == 0) {
        return dsERR_NONE;
    }

    hal_err("snd_mixer_selem_set_playback_dB_all failed.\n");
    return dsERR_GENERAL;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
}

dsError_t dsSetAudioLevel(intptr_t handle, float level)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_info("invoked.\n");
    if (!_bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }

    if (!dsAudioIsValidHandle(handle) || level < 0.0 || level > 100.0) {
        hal_err("Invalid parameters; handle(%p) or level(%f).\n", handle, level);
        return dsERR_INVALID_PARAM;
    }

    const char *s_card = ALSA_CARD_NAME;
    const char *element_name = ALSA_ELEMENT_NAME;
    snd_mixer_elem_t *mixer_elem = NULL;

    if (initAlsa(element_name, s_card, &mixer_elem) != 0 || mixer_elem == NULL) {
        hal_err("Failed to initialize ALSA!\n");
        return dsERR_GENERAL;
    }

    long min, max;
    snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
    if (level != 0) {
        min /= 2;
    }

    long vol_value = (long)(((level / 100.0) * (max - min)) + min);
    hal_info("Setting volume to %ld\n", vol_value);
    if (snd_mixer_selem_set_playback_volume_all(mixer_elem, vol_value) != 0) {
        hal_err("snd_mixer_selem_set_playback_volume_all failed.\n");
        return dsERR_GENERAL;
    }

    return dsERR_NONE;
#else // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
    hal_err("ALSA_AUDIO_MASTER_CONTROL_ENABLE is not defined.\n");
    return dsERR_OPERATION_NOT_SUPPORTED;
#endif // !ALSA_AUDIO_MASTER_CONTROL_ENABLE
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

dsError_t dsAudioPortTerm()
{
    hal_info("invoked.\n");
    dsError_t ret = dsERR_NONE;
    if (false == _bIsAudioInitialized)
    {
        return dsERR_NOT_INITIALIZED;
    }
    _halhdmiaudioCB = NULL;
    _bIsAudioInitialized = false;
    return ret;
}

bool dsCheckSurroundSupport()
{
    hal_info("invoked.\n");
    bool status = false;
    int num_channels = 0;
    for (int i=1; i<=8; i++) {
        if (vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAC3, i, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit) == 0)
            num_channels = i;
    }

    if (num_channels)
        status = true;

    return status;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t  dsEnableMS12Config(intptr_t handle, dsMS12FEATURE_t feature, const bool enable)
{
    hal_info("invoked with enable %d.\n", enable);
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) || feature < dsMS12FEATURE_DAPV2 || feature >= dsMS12FEATURE_MAX) {
        hal_err("Invalid parameters; handle(%p).\n", handle);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    if (NULL != _halhdmiaudioCB) {
        hal_warn("CBFunc already registered; overriding with new callback handle.\n");
    }
    _halhdmiaudioCB = CBFunc;
    return dsERR_NONE;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetSecondaryLanguage(intptr_t handle, const char* sLang)
{
    hal_info("invoked.\n");
    if (false == _bIsAudioInitialized) {
        return dsERR_NOT_INITIALIZED;
    }
    if (!dsAudioIsValidHandle(handle) ||  sLang == NULL) {
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}

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
    return dsERR_OPERATION_NOT_SUPPORTED;
}
