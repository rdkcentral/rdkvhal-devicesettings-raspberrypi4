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

#define ALSA_CARD_NAME "hw:0"
#if (SND_LIB_MAJOR >= 1) && (SND_LIB_MINOR >= 2)
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

static AOPHandle_t _handles[dsAUDIOPORT_TYPE_MAX][2] = {
};
float dBmin;
float dBmax;
static dsAudioEncoding_t _encoding = dsAUDIO_ENC_PCM;
static bool  _isms11Enabled = false;
static dsAudioStereoMode_t _stereoModeHDMI = dsAUDIO_STEREO_STEREO;

static void dsGetdBRange();

bool dsIsValidHandle(intptr_t uHandle)
{
    size_t index ;
    bool retValue = false;
    for (index = 0; index < dsAUDIOPORT_TYPE_MAX; index++) {
        if ((intptr_t)&_handles[index][0] == uHandle) {
            retValue = true;
            break;
        }
    }
    return retValue;
}

static int8_t initAlsa(const char *selemname, const char *s_card, snd_mixer_elem_t **element)
{
        int ret = 0;
        snd_mixer_t *smixer = NULL;
        snd_mixer_selem_id_t *sid;

        if ((ret = snd_mixer_open(&smixer, 0)) < 0) {
                printf("Cannot open sound mixer %s", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        if ((ret = snd_mixer_attach(smixer, s_card)) < 0) {
                printf("sound mixer attach Failed %s", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        if ((ret = snd_mixer_selem_register(smixer, NULL, NULL)) < 0) {
                printf("Cannot register sound mixer element %s", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        ret = snd_mixer_load(smixer);
        if (ret < 0) {
                printf("Sound mixer load %s error: %s", s_card, snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }

        ret = snd_mixer_selem_id_malloc(&sid);
        if (ret < 0) {
                printf("Sound mixer: id allocation failed. %s: error: %s", s_card, snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }

        snd_mixer_selem_id_set_index(sid, 0);
        snd_mixer_selem_id_set_name(sid, selemname);

        *element = snd_mixer_find_selem(smixer, sid);
        if (NULL == *element) {
                printf("Unable to find simple control '%s',%i\n", snd_mixer_selem_id_get_name(sid), snd_mixer_selem_id_get_index(sid));
                snd_mixer_close(smixer);
                ret = -1;
        }

        return ret;
}
//###################################################################################################
dsError_t dsAudioPortInit()
{
    dsError_t ret = dsERR_NONE;


        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_vType  = dsAUDIOPORT_TYPE_HDMI;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_nativeHandle = dsAUDIOPORT_TYPE_HDMI;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_index = 0;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_IsEnabled = true;

        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_vType  = dsAUDIOPORT_TYPE_SPDIF;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_nativeHandle = dsAUDIOPORT_TYPE_SPDIF;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_index = 0;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_IsEnabled = true;

        dsGetdBRange();
        return ret;
}

static void dsGetdBRange()
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        long min_dB_value, max_dB_value;
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        initAlsa(element_name,s_card,&mixer_elem);
        if(mixer_elem == NULL) {
                printf("failed to initialize alsa!\n");
                return;
        }
        if(!snd_mixer_selem_get_playback_dB_range(mixer_elem, &min_dB_value, &max_dB_value)) {
                dBmax = (float) max_dB_value/100;
                dBmin = (float) min_dB_value/100;
        }
#endif
}

dsError_t  dsGetAudioPort(dsAudioPortType_t type, int index, intptr_t *handle)
{
        dsError_t ret = dsERR_NONE;
        if (dsAudioType_isValid(type))
        {
                *handle = (intptr_t)&_handles[type][index];
        }
        else
        {
                ret = dsERR_NONE;
        }
        return ret;
}

dsError_t dsGetAudioEncoding(intptr_t handle, dsAudioEncoding_t *encoding)
{
        dsError_t ret = dsERR_NONE;
        *encoding = _encoding;
        return ret;
}

dsError_t dsGetAudioCompression(intptr_t handle, dsAudioCompression_t *compression)
{
    dsError_t ret = dsERR_NONE;
        if (dsIsValidHandle(handle)) {
                ret = dsERR_NONE;
        }
        else {
                ret = dsERR_NONE;
        }
        return ret;
}

dsError_t dsGetStereoMode(intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
	dsError_t ret = dsERR_NONE;
        *stereoMode = _stereoModeHDMI;
	return ret;
}

dsError_t dsGetPersistedStereoMode (intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
        return dsERR_NONE;
}

dsError_t dsGetStereoAuto (intptr_t handle, int *autoMode)
{
        return dsERR_NONE;
}

dsError_t dsIsAudioMute (intptr_t handle, bool *muted)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        printf("Inside %s :%d\n",__FUNCTION__,__LINE__);
        dsError_t ret = dsERR_NONE;
        if( ! dsIsValidHandle(handle) || NULL == muted ){
                ret = dsERR_INVALID_PARAM;
        }
	const char *s_card = ALSA_CARD_NAME;
	const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
	initAlsa(element_name,s_card,&mixer_elem);
        if(mixer_elem == NULL) {
                printf("failed to initialize alsa!\n");
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
	}
	else {
		ret = dsERR_GENERAL;
	}
	return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsSetAudioMute(intptr_t handle, bool mute)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        printf("Inside %s :%d\n",__FUNCTION__,__LINE__);
        dsError_t ret = dsERR_NONE;
        if( ! dsIsValidHandle(handle)){
                ret = dsERR_INVALID_PARAM;
        }
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        initAlsa(element_name,s_card,&mixer_elem);
        if(mixer_elem == NULL) {
                printf("failed to initialize alsa!\n");
                return dsERR_GENERAL;
        }
        if (snd_mixer_selem_has_playback_switch(mixer_elem)) {
                snd_mixer_selem_set_playback_switch_all(mixer_elem, !mute);
                if (mute) {
                        printf("Audio Mute success\n");
                } else {
                        printf("Audio Unmute success.\n");
                }
        }
        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t  dsIsAudioPortEnabled(intptr_t handle, bool *enabled)
{
    printf("Inside %s :%d\n",__FUNCTION__,__LINE__);
    dsError_t ret = dsERR_NONE;
    bool audioEnabled = true;
    ret = dsIsAudioMute(handle, &audioEnabled);
    if (ret == dsERR_NONE) {
                *enabled = !audioEnabled;
    }
    return ret;
}

dsError_t  dsEnableAudioPort(intptr_t handle, bool enabled)
{
    printf("Inside %s :%d\n",__FUNCTION__,__LINE__);
    return dsSetAudioMute ( handle, !enabled );
}

dsError_t dsGetAudioGain(intptr_t handle, float *gain)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle) || gain == NULL) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                long value_got;
                const char *s_card = ALSA_CARD_NAME;
                const char *element_name = ALSA_ELEMENT_NAME;
                long vol_min = 0, vol_max = 0;
                double normalized= 0, min_norm = 0;
                snd_mixer_elem_t *mixer_elem;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }

                snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max);
                if(!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &value_got))
                {
                    printf("dsGetAudioGain: Gain  in dB %.2f\n", value_got/100.0);
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
                        printf("dsGetAudioGain: Rounded Gain  in linear scale %.2f\n", *gain);
                    }
                }

        }
        return ret;
#else
        return dsERR_NONE;
#endif

}

dsError_t dsGetAudioDB(intptr_t handle, float *db)
{
    #ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle) || db == NULL) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                long db_value;
                const char *s_card = ALSA_CARD_NAME;
                const char *element_name = ALSA_ELEMENT_NAME;

                snd_mixer_elem_t *mixer_elem = NULL;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }

                if(!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &db_value)) {
                        *db = (float) db_value/100;
                }

        }
        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsGetAudioLevel(intptr_t handle, float *level)
{
 #ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle) ) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                long vol_value, min, max;
                const char *s_card = "default";
                const char *element_name = ALSA_ELEMENT_NAME;

                snd_mixer_elem_t *mixer_elem = NULL;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }
                if(!snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol_value)) {
	                snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
                        *level = (float)((vol_value - min)*100/(max - min));
                }

        }
        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsGetAudioMaxDB(intptr_t handle, float *maxDb)
{
        *maxDb = dBmax;
        return dsERR_NONE;
}

dsError_t dsGetAudioMinDB(intptr_t handle, float *minDb)
{
        *minDb = dBmin;
        return dsERR_NONE;
}

dsError_t dsGetAudioOptimalLevel(intptr_t handle, float *optimalLevel)
{
	dsError_t ret = dsERR_NONE;
	return ret;
}

dsError_t  dsIsAudioLoopThru(intptr_t handle, bool *loopThru)
{
	dsError_t ret = dsERR_NONE;
	return ret;
}

dsError_t dsSetAudioEncoding(intptr_t handle, dsAudioEncoding_t encoding)
{
    dsError_t ret = dsERR_NONE;
    _encoding = encoding;
    return ret;
}

dsError_t dsSetAudioCompression(intptr_t handle, dsAudioCompression_t compression)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}

dsError_t dsIsAudioMSDecode(intptr_t handle, bool *ms11Enabled)
{
    dsError_t ret = dsERR_NONE;
    *ms11Enabled = _isms11Enabled;
    return ret;
}

dsError_t dsSetStereoMode(intptr_t handle, dsAudioStereoMode_t mode) {

	dsError_t ret = dsERR_NONE;
	return ret;
}

dsError_t dsSetStereoAuto (intptr_t handle, int autoMode)
{
        return dsERR_NONE;
}

dsError_t dsSetAudioGain(intptr_t handle, float gain)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle) ) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                const char *s_card = ALSA_CARD_NAME;
                const char *element_name = ALSA_ELEMENT_NAME;
                bool enabled = false;
                ret = dsERR_GENERAL;
                snd_mixer_elem_t *mixer_elem;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }


                dsIsAudioMute(handle, &enabled);
                if (true == enabled)
                {
                    printf("dsSetAudioGain: Mute is enabled. \n");
                    return ret;
                }

                long vol_min, vol_max;
                double min_norm;
                gain = gain / 100.0f;

                snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max);
                if (vol_max - vol_min <= MAX_LINEAR_DB_SCALE * 100)
                {
                  long floatval = lrint(gain * (vol_max - vol_min)) + vol_min;
                  snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0);
                  ret = dsERR_NONE;
                }
                else
                {
                    if (vol_min != SND_CTL_TLV_DB_GAIN_MUTE)
                    {
                        min_norm = pow(10, (vol_min - vol_max) / 6000.0);
                        gain = gain * (1 - min_norm) + min_norm;
                    }
                    long floatval = lrint(6000.0 * log10(gain)) + vol_max;
                    snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0);
                    printf("dsSetAudioGain: Setting gain in dB: %.2f \n", floatval/100.0);
                    ret = dsERR_NONE;
                }
        }

        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsSetAudioDB(intptr_t handle, float db)
{
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle) ) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                const char *s_card = ALSA_CARD_NAME;
                const char *element_name = ALSA_ELEMENT_NAME;

                snd_mixer_elem_t *mixer_elem = NULL;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }

                if(db < dBmin) {
                        db = dBmin;
                }
                if(db > dBmax) {
                        db = dBmax;
                }

                if(!snd_mixer_selem_set_playback_dB_all(mixer_elem, (long) db * 100, 0)) {
                        ret = dsERR_NONE;
                }
                else {
                        ret = dsERR_GENERAL;
                }
        }
        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsSetAudioLevel(intptr_t handle, float level)
{
 #ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        dsError_t ret = dsERR_NONE;

        if( ! dsIsValidHandle(handle)) {
                ret = dsERR_INVALID_PARAM;
        }

        if ( dsERR_NONE == ret ) {
                long vol_value, min, max;
                const char *s_card = ALSA_CARD_NAME;
                const char *element_name = ALSA_ELEMENT_NAME;

                snd_mixer_elem_t *mixer_elem = NULL;
                initAlsa(element_name,s_card,&mixer_elem);
                if(mixer_elem == NULL) {
                        printf("failed to initialize alsa!\n");
                        return dsERR_GENERAL;
                }
                snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max);
                vol_value = (long)(((level / 100.0) * (max - min)) + min);
                if(snd_mixer_selem_set_playback_volume_all(mixer_elem, vol_value)) {
                    printf("Failed to set Audio level\n");
                }

        }
        return ret;
#else
        return dsERR_NONE;
#endif
}

dsError_t dsEnableLoopThru(intptr_t handle, bool loopThru)
{
	dsError_t ret = dsERR_NONE;
	return ret;
}

dsError_t dsAudioPortTerm()
{
	dsError_t ret = dsERR_NONE;
	return ret;
}

bool dsCheckSurroundSupport()
{
     bool status = false;
     int num_channels = 0;
    for (int i=1; i<=8; i++) {
      if (vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAC3, i, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit ) == 0)
        num_channels = i;
    }

    if (num_channels)
        status = true;

    return status;
}
dsError_t  dsGetAudioFormat(intptr_t handle, dsAudioFormat_t *audioFormat)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetDialogEnhancement(intptr_t handle, int *level)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetDolbyVolumeMode(intptr_t handle, bool *mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetDolbyVolumeMode(intptr_t handle, bool mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetIntelligentEqualizerMode(intptr_t handle, int *mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetIntelligentEqualizerMode(intptr_t handle, int mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetVolumeLeveller(intptr_t handle, dsVolumeLeveller_t* volLeveller)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetVolumeLeveller(intptr_t handle, dsVolumeLeveller_t volLeveller)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetBassEnhancer(intptr_t handle, int *boost)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetBassEnhancer(intptr_t handle, int boost)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsIsSurroundDecoderEnabled(intptr_t handle, bool *enabled)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsEnableSurroundDecoder(intptr_t handle, bool enabled)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetDRCMode(intptr_t handle, int *mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetDRCMode(intptr_t handle, int mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetSurroundVirtualizer(intptr_t handle, dsSurroundVirtualizer_t *virtualizer)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetSurroundVirtualizer(intptr_t handle, dsSurroundVirtualizer_t virtualizer)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetMISteering(intptr_t handle, bool *enabled)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetMISteering(intptr_t handle, bool enabled)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetGraphicEqualizerMode(intptr_t handle, int *mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetGraphicEqualizerMode(intptr_t handle, int mode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetMS12AudioProfileList(intptr_t handle, dsMS12AudioProfileList_t* profiles)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetMS12AudioProfile(intptr_t handle, char *profile)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetSupportedARCTypes(intptr_t handle, int *types)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioSetSAD(intptr_t handle, dsAudioSADList_t sad_list)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioEnableARC(intptr_t handle, dsAudioARCStatus_t arcStatus)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetAudioDelay(intptr_t handle, uint32_t *audioDelayMs)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetAudioDelay(intptr_t handle, const uint32_t audioDelayMs)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetAudioDelayOffset(intptr_t handle, uint32_t *audioDelayOffsetMs)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetAudioDelayOffset(intptr_t handle, const uint32_t audioDelayOffsetMs)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetAudioAtmosOutputMode(intptr_t handle, bool enable)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetSinkDeviceAtmosCapability(intptr_t handle, dsATMOSCapability_t *capability)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsEnableMS12Config(intptr_t handle, dsMS12FEATURE_t feature,const bool enable)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsEnableLEConfig(intptr_t handle, const bool enable)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetLEConfig(intptr_t handle, bool *enable)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetMS12AudioProfile(intptr_t handle, const char* profile)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetAudioDucking(intptr_t handle, dsAudioDuckingAction_t action, dsAudioDuckingType_t type, const unsigned char level)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsIsAudioMS12Decode(intptr_t handle, bool *hasMS12Decode)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioOutIsConnected(intptr_t handle, bool* isConnected)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioOutRegisterConnectCB(dsAudioOutPortConnectCB_t CBFunc)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioFormatUpdateRegisterCB(dsAudioFormatUpdateCB_t cbFun)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsAudioAtmosCapsChangeRegisterCB (dsAtmosCapsChangeCB_t cbFun)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetAudioCapabilities(intptr_t handle, int *capabilities)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetMS12Capabilities(intptr_t handle, int *capabilities)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsResetDialogEnhancement(intptr_t handle)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsResetBassEnhancer(intptr_t handle)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsResetSurroundVirtualizer(intptr_t handle)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsResetVolumeLeveller(intptr_t handle)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetAssociatedAudioMixing(intptr_t handle, bool mixing)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetAssociatedAudioMixing(intptr_t handle, bool *mixing)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetFaderControl(intptr_t handle, int mixerbalance)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetFaderControl(intptr_t handle, int* mixerbalance)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetPrimaryLanguage(intptr_t handle, const char* pLang)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetPrimaryLanguage(intptr_t handle, char* pLang)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsSetSecondaryLanguage(intptr_t handle, const char* sLang)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t  dsGetSecondaryLanguage(intptr_t handle, char* sLang)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsGetHDMIARCPortId(int *portId)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
dsError_t dsSetAudioMixerLevels (intptr_t handle, dsAudioInput_t aInput, int volume)
{
        dsError_t ret = dsERR_NONE;
        return ret;
}
