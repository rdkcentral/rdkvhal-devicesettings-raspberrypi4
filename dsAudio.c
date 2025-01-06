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
#if (SND_LIB_MAJOR >= 1) && (SND_LIB_MINOR >= 2)
#define ALSA_ELEMENT_NAME "HDMI"
#else
#define ALSA_ELEMENT_NAME "PCM"
#endif

#define MAX_LINEAR_DB_SCALE 24

typedef struct _AOPHandle_t
{
        dsAudioPortType_t m_vType;
        int m_index;
        int m_nativeHandle;
        bool m_IsEnabled;
} AOPHandle_t;

static AOPHandle_t _handles[dsAUDIOPORT_TYPE_MAX][2] = {};
float dBmin;
float dBmax;
static dsAudioEncoding_t _encoding = dsAUDIO_ENC_PCM;
static bool _isms11Enabled = false;
static dsAudioStereoMode_t _stereoModeHDMI = dsAUDIO_STEREO_STEREO;
static bool _bIsAudioInitialized = false;

static void dsGetdBRange();

bool dsAudioIsValidHandle(intptr_t uHandle)
{
        bool retValue = false;
        for (size_t index = 0; index < dsAUDIOPORT_TYPE_MAX; index++)
        {
                if ((intptr_t)&_handles[index][0] == uHandle)
                {
                        retValue = true;
                        break;
                }
        }
        return retValue;
}

static int8_t initAlsa(const char *selemname, const char *s_card, snd_mixer_elem_t **element)
{
        hal_dbg("invoked.\n");
        int ret = 0;
        snd_mixer_t *smixer = NULL;
        snd_mixer_selem_id_t *sid;

        if ((ret = snd_mixer_open(&smixer, 0)) < 0)
        {
                hal_err("Cannot open sound mixer '%s'\n", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        if ((ret = snd_mixer_attach(smixer, s_card)) < 0)
        {
                hal_err("Cannot attach sound mixer '%s'\n", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        if ((ret = snd_mixer_selem_register(smixer, NULL, NULL)) < 0)
        {
                hal_err("Cannot register sound mixer element '%s'\n", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }
        ret = snd_mixer_load(smixer);
        if (ret < 0)
        {
                hal_err("Cannot load sound mixer '%s'\n", snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }

        ret = snd_mixer_selem_id_malloc(&sid);
        if (ret < 0)
        {
                hal_err("Sound mixer: id allocation failed. '%s': error: '%s'\n", s_card, snd_strerror(ret));
                snd_mixer_close(smixer);
                return ret;
        }

        snd_mixer_selem_id_set_index(sid, 0);
        snd_mixer_selem_id_set_name(sid, selemname);

        *element = snd_mixer_find_selem(smixer, sid);
        if (NULL == *element)
        {
                hal_err("Unable to find simple control '%s',%i\n", (sid), snd_mixer_selem_id_get_index(sid));
                snd_mixer_close(smixer);
                ret = -1;
        }

        return ret;
}

static void dsGetdBRange()
{
        hal_dbg("Invoked.\n");
#ifdef ALSA_AUDIO_MASTER_CONTROL_ENABLE
        long min_dB_value, max_dB_value;
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        initAlsa(element_name, s_card, &mixer_elem);
        if (mixer_elem == NULL)
        {
                hal_err("initAlsa failed.\n");
                return;
        }
        if (!snd_mixer_selem_get_playback_dB_range(mixer_elem, &min_dB_value, &max_dB_value))
        {
                dBmax = (float)max_dB_value / 100;
                dBmin = (float)min_dB_value / 100;
        }
        else
        {
                hal_err("snd_mixer_selem_get_playback_dB_range failed.\n");
        }
#endif
}

/**********************************************************************************************/
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
        hal_dbg("invoked.\n");
        dsError_t ret = dsERR_NONE;
        if (_bIsAudioInitialized)
        {
                return dsERR_ALREADY_INITIALIZED;
        }

        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_vType = dsAUDIOPORT_TYPE_HDMI;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_nativeHandle = dsAUDIOPORT_TYPE_HDMI;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_index = 0;
        _handles[dsAUDIOPORT_TYPE_HDMI][0].m_IsEnabled = true;

        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_vType = dsAUDIOPORT_TYPE_SPDIF;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_nativeHandle = dsAUDIOPORT_TYPE_SPDIF;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_index = 0;
        _handles[dsAUDIOPORT_TYPE_SPDIF][0].m_IsEnabled = true;

        dsGetdBRange();
        _bIsAudioInitialized = true;
        return ret;
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
        hal_dbg("Invoked.\n");
        dsError_t ret = dsERR_NONE;
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        _bIsAudioInitialized = false;
        return ret;
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == handle || 0 != index || !dsAudioType_isValid(type))
        {
                return dsERR_INVALID_PARAM;
        }
        else
        {
                *handle = (intptr_t)&_handles[type][index];
        }
        return dsERR_NONE;
}

/**
 * @brief maps the audio format to dsAudioEncoding_t
 * @param[in] format - audio format
 * @return dsAudioEncoding_t - audio encoding type
 */
dsAudioEncoding_t mapAudioFormat(snd_pcm_format_t format)
{
        switch (format)
        {
        case SND_PCM_FORMAT_S8:
        case SND_PCM_FORMAT_U8:
        case SND_PCM_FORMAT_S16_LE:
        case SND_PCM_FORMAT_S16_BE:
        case SND_PCM_FORMAT_U16_LE:
        case SND_PCM_FORMAT_U16_BE:
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
        case SND_PCM_FORMAT_U24_LE:
        case SND_PCM_FORMAT_U24_BE:
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
        case SND_PCM_FORMAT_U32_LE:
        case SND_PCM_FORMAT_U32_BE:
        case SND_PCM_FORMAT_FLOAT_LE:
        case SND_PCM_FORMAT_FLOAT_BE:
        case SND_PCM_FORMAT_FLOAT64_LE:
        case SND_PCM_FORMAT_FLOAT64_BE:
        case SND_PCM_FORMAT_IEC958_SUBFRAME_LE:
        case SND_PCM_FORMAT_IEC958_SUBFRAME_BE:
        case SND_PCM_FORMAT_MU_LAW:
        case SND_PCM_FORMAT_A_LAW:
        case SND_PCM_FORMAT_IMA_ADPCM:
        case SND_PCM_FORMAT_MPEG:
        case SND_PCM_FORMAT_GSM:
        case SND_PCM_FORMAT_S20_LE:
        case SND_PCM_FORMAT_S20_BE:
        case SND_PCM_FORMAT_U20_LE:
        case SND_PCM_FORMAT_U20_BE:
        case SND_PCM_FORMAT_SPECIAL:
        case SND_PCM_FORMAT_S24_3LE:
        case SND_PCM_FORMAT_S24_3BE:
        case SND_PCM_FORMAT_U24_3LE:
        case SND_PCM_FORMAT_U24_3BE:
        case SND_PCM_FORMAT_S20_3LE:
        case SND_PCM_FORMAT_S20_3BE:
        case SND_PCM_FORMAT_U20_3LE:
        case SND_PCM_FORMAT_U20_3BE:
        case SND_PCM_FORMAT_S18_3LE:
        case SND_PCM_FORMAT_S18_3BE:
        case SND_PCM_FORMAT_U18_3LE:
        case SND_PCM_FORMAT_U18_3BE:
                return dsAUDIO_ENC_PCM;
#if 0 // Not defined ?
        case SND_PCM_FORMAT_AC3:
                return dsAUDIO_ENC_AC3;
        case SND_PCM_FORMAT_EAC3:
                return dsAUDIO_ENC_EAC3;
#endif
        default:
                return dsAUDIO_ENC_NONE;
        }
}

/**
 * @brief Gets the encoding type of an audio port
 *
 * This function returns the current audio encoding setting for the specified audio port.
 *
 * @param[in] handle     -  Handle for the output audio port
 * @param[out] encoding  -  Pointer to hold the encoding setting of the audio port. Please refer ::dsAudioEncoding_t , @link dsAudioSettings_template.h @endlink
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre  dsAudioPortInit() and dsGetAudioPort() should be called in this order before calling this API.
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetAudioEncoding()
 */
dsError_t dsGetAudioEncoding(intptr_t handle, dsAudioEncoding_t *encoding)
{
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                hal_err("Audio not initialized.\n");
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == encoding || !dsAudioIsValidHandle(handle))
        {
                hal_err("Invalid parameter.\n");
                return dsERR_INVALID_PARAM;
        }
        *encoding = _encoding;
        return dsERR_NONE;
}

/**
 * @brief maps the audio encoding to snd_pcm_format_t
 * @param[in] encoding - RDK audio encoding type
 * @return snd_pcm_format_t - audio format
 */
snd_pcm_format_t mapToAlsaFormat(dsAudioEncoding_t encoding)
{
        switch (encoding)
        {
        case dsAUDIO_ENC_PCM:
                return SND_PCM_FORMAT_S16_LE; // Example format, choose the appropriate one
        case dsAUDIO_ENC_AC3:
                // return SND_PCM_FORMAT_AC3; // Alsa says undefined ?
        case dsAUDIO_ENC_EAC3:
                // return SND_PCM_FORMAT_EAC3; // Alsa says undefined ?
        default:
                return SND_PCM_FORMAT_UNKNOWN;
        }
}

/**
 * @brief Sets the encoding type of an audio port
 *
 * This function sets the audio encoding type to be used on the specified audio port.
 *
 * @param[in] handle    - Handle for the output audio port
 * @param[in] encoding  - The encoding type to be used on the audio port. Please refer ::dsAudioEncoding_t
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
 * @see dsGetAudioEncoding()
 */
dsError_t dsSetAudioEncoding(intptr_t handle, dsAudioEncoding_t encoding)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        _encoding = encoding;
        return dsERR_NONE;
}

/**
 * @brief maps the audio format to dsAudioFormat_t
 * @param[in] format - audio format
 * @return dsAudioFormat_t - audio format type
 */
dsAudioFormat_t mapToAudioFormat(snd_pcm_format_t format)
{
        switch (format)
        {
        case SND_PCM_FORMAT_S8:
        case SND_PCM_FORMAT_U8:
        case SND_PCM_FORMAT_S16_LE:
        case SND_PCM_FORMAT_S16_BE:
        case SND_PCM_FORMAT_U16_LE:
        case SND_PCM_FORMAT_U16_BE:
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
        case SND_PCM_FORMAT_U24_LE:
        case SND_PCM_FORMAT_U24_BE:
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
        case SND_PCM_FORMAT_U32_LE:
        case SND_PCM_FORMAT_U32_BE:
        case SND_PCM_FORMAT_FLOAT_LE:
        case SND_PCM_FORMAT_FLOAT_BE:
        case SND_PCM_FORMAT_FLOAT64_LE:
        case SND_PCM_FORMAT_FLOAT64_BE:
        case SND_PCM_FORMAT_IEC958_SUBFRAME_LE:
        case SND_PCM_FORMAT_IEC958_SUBFRAME_BE:
        case SND_PCM_FORMAT_MU_LAW:
        case SND_PCM_FORMAT_A_LAW:
        case SND_PCM_FORMAT_IMA_ADPCM:
        case SND_PCM_FORMAT_MPEG:
        case SND_PCM_FORMAT_GSM:
        case SND_PCM_FORMAT_S20_LE:
        case SND_PCM_FORMAT_S20_BE:
        case SND_PCM_FORMAT_U20_LE:
        case SND_PCM_FORMAT_U20_BE:
        case SND_PCM_FORMAT_SPECIAL:
        case SND_PCM_FORMAT_S24_3LE:
        case SND_PCM_FORMAT_S24_3BE:
        case SND_PCM_FORMAT_U24_3LE:
        case SND_PCM_FORMAT_U24_3BE:
        case SND_PCM_FORMAT_S20_3LE:
        case SND_PCM_FORMAT_S20_3BE:
        case SND_PCM_FORMAT_U20_3LE:
        case SND_PCM_FORMAT_U20_3BE:
        case SND_PCM_FORMAT_S18_3LE:
        case SND_PCM_FORMAT_S18_3BE:
        case SND_PCM_FORMAT_U18_3LE:
        case SND_PCM_FORMAT_U18_3BE:
                return dsAUDIO_FORMAT_PCM;
#if 0 // Not defined ?
        case SND_PCM_FORMAT_AC3:
                return dsAUDIO_FORMAT_DOLBY_AC3;
        case SND_PCM_FORMAT_EAC3:
                return dsAUDIO_FORMAT_DOLBY_EAC3;
#endif
        default:
                return dsAUDIO_FORMAT_UNKNOWN;
        }
}

/**
 * @brief Gets the current audio format.
 *
 * This function returns the current audio format of the specified audio output port(like PCM, DOLBY AC3). Please refer ::dsAudioFormat_t
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == audioFormat || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief maps the audio compression value to dsAudioCompression_t
 * @param[in] value - audio compression value
 * @return dsAudioCompression_t - audio compression type
 */
dsAudioCompression_t mapCompressionValue(long value)
{
        if (value <= 2)
                return dsAUDIO_CMP_LIGHT;
        else if (value <= 5)
                return dsAUDIO_CMP_MEDIUM;
        else if (value <= 10)
                return dsAUDIO_CMP_HEAVY;
        else
                return dsAUDIO_CMP_NONE;
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == compression || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief maps the RDK audio compression level to long
 * @param[in] compression - audio compression level
 * @return long - audio compression value
 */
long mapCompressionLevel(int compression)
{
        if (compression < 0)
                return 0;
        else if (compression > 10)
                return 10;
        else
                return compression;
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || compression < 0 || compression > 10)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Dialog Enhancement level of the audio port.
 *
 * This function returns the dialog enhancement level of the audio port corresponding to the specified port handle.
 *
 * @param[in] handle - Handle for the output audio port
 * @param[out] level - Pointer to Dialog Enhancement level (Value ranges from 0 to 16)
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (level == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Dialog Enhancement level of an audio port.
 *
 * This function sets the dialog enhancement level to be used in the audio port corresponding to specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port.
 * @param[in] level   - Dialog Enhancement level. Level ranges from 0 to 16.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || level < 0 || level > 16)
        {
                return dsERR_INVALID_PARAM;
        }
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (mode == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Intelligent Equalizer Mode.
 *
 * This function returns the Intelligent Equalizer Mode setting used in the audio port corresponding to specified Port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (mode == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Intelligent Equalizer Mode.
 *
 * This function sets the Intelligent Equalizer Mode to be used in the audio port corresponding to the specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mode < 0 || mode > 6)
        {
                return dsERR_INVALID_PARAM;
        }
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
dsError_t dsGetVolumeLeveller(intptr_t handle, dsVolumeLeveller_t *volLeveller)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (volLeveller == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (volLeveller.mode < 0 || volLeveller.mode > 2 ||
            volLeveller.level < 0 || volLeveller.level > 10 || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Bass
 *
 * This function returns the Bass used in a given audio port
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (boost == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio Bass
 *
 * This function sets the Bass to be used in the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (boost < 0 || boost > 100 || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Surround Decoder enabled/disabled status
 *
 * This function returns enable/disable status of surround decoder
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (enabled == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables / Disables the audio Surround Decoder.
 *
 * This function will enable/disable surround decoder of the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (mode == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (mode < 0 || mode > 1 || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio Surround Virtualizer level.
 *
 * This function returns the Surround Virtualizer level(mode and boost) used in the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (virtualizer == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio Surround Virtualizer level
 *
 * This function sets the Surround Virtualizer level(mode and boost) to be used in the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Media Intelligent Steering of the audio port.
 *
 * This function returns enable/disable status of Media Intelligent Steering for the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (enabled == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Set the Media Intelligent Steering of the audio port.
 *
 * This function sets the enable/disable status of Media Intelligent Steering for the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the Graphic Equalizer Mode.
 *
 * This function returns the Graphic Equalizer Mode setting used in the audio port corresponding to the specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (mode == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Graphic Equalizer Mode.
 *
 * This function sets the Graphic Equalizer Mode setting to be used in the audio port corresponding to the specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mode < 0 || mode > 3)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported MS12 audio profiles
 *
 * This function will get the list of supported MS12 audio profiles
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (profiles == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets current audio profile selection
 *
 * This function gets the current audio profile configured
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (profile == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported ARC types of the connected ARC/eARC device
 *
 * This function gets the supported ARC types of the connected device on ARC/eARC port.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (types == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets Short Audio Descriptor retrieved from CEC for the connected ARC device
 *
 * This function sets the Short Audio Descriptor based on best available options
 * of Audio capabilities supported by connected ARC device. Required when ARC output
 * mode is Auto/Passthrough. Please refer ::dsAudioSADList_t, ::dsSetStereoMode
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enable/Disable ARC/EARC and route audio to connected device.
 *
 * This function enables/disables ARC/EARC and routes audio to connected device. Please refer ::_dsAudioARCStatus_t and ::dsAudioARCTypes_t
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the stereo mode of an audio port.
 *
 * This function sets the stereo mode to be used on the audio port corresponding to specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] mode    - Stereo mode to be used on the specified audio port. Please refer ::dsAudioStereoMode_t
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mode >= dsAUDIO_STEREO_MAX || mode <= dsAUDIO_STEREO_UNKNOWN)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Checks if auto mode is enabled or not for the current audio port.
 *
 * This function returns the current auto mode of audio port corresponding to specified port handle.
 *
 * @param[in] handle     - Handle for the output audio port
 * @param[out] autoMode  - Pointer to hold the auto mode setting ( @a if enabled, @a false if disabled) of the specified audio port
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == autoMode || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the Auto Mode to be used on the audio port.
 *
 * This function sets the auto mode to be used on the specified audio port.
 *
 * @param[in] handle    - Handle for the output audio port.
 * @param[in] autoMode  - Indicates the auto mode ( @a true if enabled, @a false if disabled ) to be used on audio port.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || autoMode < 0)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio gain of an audio port.
 *
 * This function returns the current audio gain for the audio port corresponding to specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] gain   - Pointer to hold the audio gain value of the specified audio port.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }

        if (!dsAudioIsValidHandle(handle) || gain == NULL)
        {
                return dsERR_INVALID_PARAM;
        }

        long value_got;
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        long vol_min = 0, vol_max = 0;
        double normalized = 0, min_norm = 0;
        snd_mixer_elem_t *mixer_elem;
        if (initAlsa(element_name, s_card, &mixer_elem) != dsERR_NONE)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max) != 0)
        {
                hal_err("failed to get playback dB range\n");
                return dsERR_GENERAL;
        }
        if (!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &value_got))
        {
                hal_dbg(" dsGetAudioGain: Gain in dB %ld (%.2f)\n", value_got, value_got / 100.0);
                if ((vol_max - vol_min) <= MAX_LINEAR_DB_SCALE * 100)
                {
                        *gain = (value_got - vol_min) / (double)(vol_max - vol_min);
                        hal_dbg("%ld dsGetAudioGain: Gain %.2f\n", *gain);
                }
                else
                {
                        normalized = pow(10, (value_got - vol_max) / 6000.0);
                        if (vol_min != SND_CTL_TLV_DB_GAIN_MUTE)
                        {
                                min_norm = pow(10, (vol_min - vol_max) / 6000.0);
                                normalized = (normalized - min_norm) / (1 - min_norm);
                        }
                        *gain = (float)(((int)(100.0f * normalized + 0.5f)) / 1.0f);
                        hal_dbg("%ld dsGetAudioGain: Gain %.2f\n", *gain);
                }
        }
        else
        {
                hal_err("failed to get playback dB\n");
                return dsERR_GENERAL;
        }
        hal_warn("%s: dsGetAudioGain: Gain %.2f\n", *gain);
        return dsERR_NONE;
}

/**
 * @brief Sets the audio gain of an audio port.
 *
 * This function sets the gain to be used on the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        bool enabled = false;
        snd_mixer_elem_t *mixer_elem;
        initAlsa(element_name, s_card, &mixer_elem);
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }

        if (dsIsAudioMute(handle, &enabled) != dsERR_NONE)
        {
                hal_err("failed to get mute status\n");
                return dsERR_GENERAL;
        }
        if (true == enabled)
        {
                hal_err("failed to set gain, mute is enabled\n");
                return dsERR_GENERAL;
        }

        long vol_min, vol_max;
        double min_norm;
        gain = gain / 100.0f;
        if (snd_mixer_selem_get_playback_dB_range(mixer_elem, &vol_min, &vol_max) != 0)
        {
                hal_err("failed to get playback dB range\n");
                return dsERR_GENERAL;
        }
        if ((vol_max - vol_min) <= MAX_LINEAR_DB_SCALE * 100)
        {
                long floatval = lrint(gain * (vol_max - vol_min)) + vol_min;
                if (snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0) != 0)
                {
                        hal_err("failed to set playback dB\n");
                        return dsERR_GENERAL;
                }
        }
        else
        {
                if (vol_min != SND_CTL_TLV_DB_GAIN_MUTE)
                {
                        min_norm = pow(10, (vol_min - vol_max) / 6000.0);
                        gain = gain * (1 - min_norm) + min_norm;
                }
                long floatval = lrint(6000.0 * log10(gain)) + vol_max;
                if (snd_mixer_selem_set_playback_dB_all(mixer_elem, floatval, 0) != 0)
                {
                        hal_err("failed to set playback dB\n");
                        return dsERR_GENERAL;
                }
                hal_dbg(" Setting gain in dB: %.2f \n", floatval / 100.0);
        }
        return dsERR_NONE;
}

/**
 * @brief Gets the current audio dB level of an audio port.
 *
 * This function returns the current audio dB level for the audio port corresponding to specified port handle.
 * The Audio dB level ranges from -1450 to 180 dB
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] db     - Pointer to hold the Audio dB level of the specified audio port
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
 * @see dsSetAudioDB()
 */
dsError_t dsGetAudioDB(intptr_t handle, float *db)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || db == NULL)
        {
                return dsERR_INVALID_PARAM;
        }

        long db_value;
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;

        if (initAlsa(element_name, s_card, &mixer_elem) != dsERR_NONE)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }

        if (!snd_mixer_selem_get_playback_dB(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &db_value))
        {
                *db = (float)db_value / 100;
        }
        hal_warn("%s: Gain DB %.2f\n", *db);
        return dsERR_NONE;
}

/**
 * @brief Sets the current audio dB level of an audio port.
 *
 * This function sets the dB level to be used on the audio port corresponding to specified port handle.
 * Max dB is 180 and Min dB is -1450
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[in] db      - Audio dB level to be used on the audio port
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
 * @see dsGetAudioDB()
 */
dsError_t dsSetAudioDB(intptr_t handle, float db)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }

        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        if (initAlsa(element_name, s_card, &mixer_elem) != dsERR_NONE)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, mixer_elem NULL!\n");
                return dsERR_GENERAL;
        }

        if (db < dBmin)
        {
                db = dBmin;
        }
        if (db > dBmax)
        {
                db = dBmax;
        }

        if (snd_mixer_selem_set_playback_dB_all(mixer_elem, (long)db * 100, 0) != 0)
        {
                hal_err("failed to set snd_mixer_selem_set_playback_dB_all\n");
                return dsERR_GENERAL;
        }
        return dsERR_NONE;
}

/**
 * @brief Gets the current audio volume level of an audio port.
 *
 * This function returns the current audio volume level of audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == level)
        {
                return dsERR_INVALID_PARAM;
        }

        long vol_value, min, max;
        const char *s_card = "default";
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;

        if (initAlsa(element_name, s_card, &mixer_elem) != dsERR_NONE)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, mixer_elem NULL!\n");
                return dsERR_GENERAL;
        }
        if (!snd_mixer_selem_get_playback_volume(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &vol_value))
        {
                if (snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max) != 0)
                {
                        hal_err("failed to get playback volume range\n");
                        return dsERR_GENERAL;
                }
                if (min != vol_value)
                {
                        min = min / 2;
                }
                *level = round((float)((vol_value - min) * 100.0 / (max - min)));
        }
        hal_warn("%s: Volume Level %.2f\n", *level);
        return dsERR_NONE;
}

/**
 * @brief Sets the audio volume level of an audio port.
 *
 * This function sets the audio volume level to be used on the audio port corresponding to specified port handle.
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
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || level < 0.0 || level > 100.0)
        {
                return dsERR_INVALID_PARAM;
        }

        long vol_value, min, max;
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;

        if (initAlsa(element_name, s_card, &mixer_elem) != dsERR_NONE)
        {
                hal_err("failed to initialize alsa, initAlsa!\n");
                return dsERR_GENERAL;
        }
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, mixer_elem NULL!\n");
                return dsERR_GENERAL;
        }
        if (snd_mixer_selem_get_playback_volume_range(mixer_elem, &min, &max) != 0)
        {
                hal_err("failed to get playback volume range\n");
                return dsERR_GENERAL;
        }
        if (level != 0)
        {
                min = min / 2;
        }
        vol_value = (long)(((level / 100.0) * (max - min)) + min);
        if (snd_mixer_selem_set_playback_volume_all(mixer_elem, vol_value) != 0)
        {
                hal_err("failed to set playback volume\n");
                return dsERR_GENERAL;
        }
        else
        {
                hal_dbg(" Set volume to %ld\n", vol_value);
        }
        return dsERR_NONE;
}

/**
 * @brief Gets the maximum audio dB level of an audio port.
 *
 * This function returns the maximum audio dB level supported by the audio port corresponding to specified port handle(platform specific).
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] maxDb  - Pointer to hold the maximum audio dB value (float value e.g:10.0) supported by the specified audio port(platform specific)
 *                        Maximum value can be 180 dB
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
dsError_t dsGetAudioMaxDB(intptr_t handle, float *maxDb)
{
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                hal_err("Audio not initialized.\n");
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == maxDb)
        {
                hal_err("Invalid parameter.\n");
                return dsERR_INVALID_PARAM;
        }
        *maxDb = dBmax;
        hal_dbg("Maximum dB level is %f.\n", *maxDb);
        return dsERR_NONE;
}

/**
 * @brief Gets the minimum audio dB level of an audio port.
 *
 * This function returns the minimum audio dB level supported by the audio port corresponding to specified port handle.
 *
 * @param[in] handle  - Handle for the output audio port
 * @param[out] minDb  - Pointer to hold the minimum audio dB value (float. e.g: 0.0) supported by the specified audio port(platform specific)
 *                        Minimum value can be -1450 dB
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
dsError_t dsGetAudioMinDB(intptr_t handle, float *minDb)
{
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                hal_err("Audio not initialized.\n");
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == minDb)
        {
                hal_err("Invalid parameter.\n");
                return dsERR_INVALID_PARAM;
        }
        *minDb = dBmin;
        hal_dbg("Minimum dB level is %f.\n", *minDb);
        return dsERR_NONE;
}

/**
 * @brief Gets the optimal audio level of an audio port.
 *
 * This function returns the optimal audio level (dB) of the audio port corresponding to specified port handle(platform specific).
 *
 * @param[in] handle        - Handle for the output audio port
 * @param[out] optimalLevel - Pointer to hold the optimal level value of the specified audio port(platform specific)
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
dsError_t dsGetAudioOptimalLevel(intptr_t handle, float *optimalLevel)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == optimalLevel)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio delay (in ms) of an audio port
 *
 * This function returns the audio delay (in milliseconds) of audio port with respect to video corresponding to the specified port handle.
 *
 * @param[in] handle        - Handle for the output Audio port
 * @param[out] audioDelayMs - Pointer to Audio delay ( ranges from 0 to 200 milliseconds )
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
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (audioDelayMs == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio delay (in ms) of an audio port.
 *
 * This function will set the audio delay (in milliseconds) of audio port corresponding to the specified port handle.
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
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || audioDelayMs > 200)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the audio delay offset (in ms) of an audio port.
 *
 * This function returns the audio delay offset (in milliseconds) of the audio port corresponding to specified port handle.
 *
 * @param[in] handle               - Handle for the output Audio port
 * @param[out] audioDelayOffsetMs  - Audio delay offset in milliseconds
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
 * @see dsSetAudioDelayOffset()
 */
dsError_t dsGetAudioDelayOffset(intptr_t handle, uint32_t *audioDelayOffsetMs)
{
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (audioDelayOffsetMs == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio delay offset (in ms) of an audio port.
 *
 * This function will set the audio delay offset (in milliseconds) of the audio port corresponding to specified port handle.
 *
 * @param[in] handle              - Handle for the output Audio port
 * @param[in] audioDelayOffsetMs  - Amount of delay offset(in milliseconds)
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
 * @see dsGetAudioDelayOffset()
 */
dsError_t dsSetAudioDelayOffset(intptr_t handle, const uint32_t audioDelayOffsetMs)
{
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the audio ATMOS output mode.
 *
 * This function will set the Audio Atmos output mode.
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
        hal_dbg("invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

//=============================================================================

dsError_t dsGetStereoMode(intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
        hal_dbg("Invoked.\n");
        dsError_t ret = dsERR_NONE;
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == stereoMode || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        *stereoMode = _stereoModeHDMI;
        return ret;
}

dsError_t dsGetPersistedStereoMode(intptr_t handle, dsAudioStereoMode_t *stereoMode)
{
        hal_dbg("Invoked.\n");
        return dsERR_NONE;
}

dsError_t dsIsAudioMute(intptr_t handle, bool *muted)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == muted)
        {
                return dsERR_INVALID_PARAM;
        }
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        initAlsa(element_name, s_card, &mixer_elem);
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, mixer_elem is NULL!\n");
                return dsERR_GENERAL;
        }
        int mute_status;
        if (snd_mixer_selem_has_playback_switch(mixer_elem))
        {
                snd_mixer_selem_get_playback_switch(mixer_elem, SND_MIXER_SCHN_FRONT_LEFT, &mute_status);
                if (!mute_status)
                {
                        *muted = true;
                }
                else
                {
                        *muted = false;
                }
        }
        else
        {
                return dsERR_GENERAL;
        }
        return dsERR_NONE;
}

dsError_t dsSetAudioMute(intptr_t handle, bool mute)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        const char *s_card = ALSA_CARD_NAME;
        const char *element_name = ALSA_ELEMENT_NAME;
        snd_mixer_elem_t *mixer_elem = NULL;
        initAlsa(element_name, s_card, &mixer_elem);
        if (mixer_elem == NULL)
        {
                hal_err("failed to initialize alsa, mixer_elem is NULL!\n");
                return dsERR_GENERAL;
        }
        if (snd_mixer_selem_has_playback_switch(mixer_elem))
        {
                snd_mixer_selem_set_playback_switch_all(mixer_elem, !mute);
                if (mute)
                {
                        hal_dbg("Audio Mute success.\n");
                }
                else
                {
                        hal_dbg("Audio Unmute success.\n");
                }
        }
        return dsERR_NONE;
}

dsError_t dsIsAudioPortEnabled(intptr_t handle, bool *enabled)
{
        hal_dbg("Invoked.\n");
        bool audioEnabled = true;
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (NULL == enabled || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        if (dsIsAudioMute(handle, &audioEnabled) == dsERR_NONE)
        {
                *enabled = !audioEnabled;
                return dsERR_NONE;
        }
        return dsERR_GENERAL;
}

dsError_t dsEnableAudioPort(intptr_t handle, bool enabled)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsSetAudioMute(handle, !enabled);
}

dsError_t dsIsAudioLoopThru(intptr_t handle, bool *loopThru)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == loopThru)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsIsAudioMSDecode(intptr_t handle, bool *ms11Enabled)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || NULL == ms11Enabled)
        {
                return dsERR_INVALID_PARAM;
        }
        *ms11Enabled = _isms11Enabled;
        return dsERR_NONE;
}

dsError_t dsEnableLoopThru(intptr_t handle, bool loopThru)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

bool dsCheckSurroundSupport()
{
        hal_dbg("Invoked.\n");
        bool status = false;
        int num_channels = 0;
        for (int i = 1; i <= 8; i++)
        {
                if (vc_tv_hdmi_audio_supported(EDID_AudioFormat_eAC3, i, EDID_AudioSampleRate_e44KHz, EDID_AudioSampleSize_16bit) == 0)
                        num_channels = i;
        }

        if (num_channels)
                status = true;

        return status;
}

dsError_t dsGetSinkDeviceAtmosCapability(intptr_t handle, dsATMOSCapability_t *capability)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (capability == NULL || !dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsEnableMS12Config(intptr_t handle, dsMS12FEATURE_t feature, const bool enable)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsEnableLEConfig(intptr_t handle, const bool enable)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetLEConfig(intptr_t handle, bool *enable)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || enable == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetMS12AudioProfile(intptr_t handle, const char *profile)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || profile == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetAudioDucking(intptr_t handle, dsAudioDuckingAction_t action, dsAudioDuckingType_t type, const unsigned char level)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || level > 100)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsIsAudioMS12Decode(intptr_t handle, bool *hasMS12Decode)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || hasMS12Decode == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsAudioOutIsConnected(intptr_t handle, bool *isConnected)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || isConnected == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsAudioOutRegisterConnectCB(dsAudioOutPortConnectCB_t CBFunc)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (CBFunc == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsAudioFormatUpdateRegisterCB(dsAudioFormatUpdateCB_t cbFun)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (cbFun == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsAudioAtmosCapsChangeRegisterCB(dsAtmosCapsChangeCB_t cbFun)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (cbFun == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetAudioCapabilities(intptr_t handle, int *capabilities)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || capabilities == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetMS12Capabilities(intptr_t handle, int *capabilities)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || capabilities == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsResetDialogEnhancement(intptr_t handle)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsResetBassEnhancer(intptr_t handle)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsResetSurroundVirtualizer(intptr_t handle)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsResetVolumeLeveller(intptr_t handle)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetAssociatedAudioMixing(intptr_t handle, bool mixing)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle))
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetAssociatedAudioMixing(intptr_t handle, bool *mixing)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mixing == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetFaderControl(intptr_t handle, int mixerbalance)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mixerbalance < -32 || mixerbalance > 32)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetFaderControl(intptr_t handle, int *mixerbalance)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || mixerbalance == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetPrimaryLanguage(intptr_t handle, const char *pLang)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || pLang == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetPrimaryLanguage(intptr_t handle, char *pLang)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || pLang == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetSecondaryLanguage(intptr_t handle, const char *sLang)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || sLang == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetSecondaryLanguage(intptr_t handle, char *sLang)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || sLang == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsGetHDMIARCPortId(int *portId)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (portId == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetAudioMixerLevels(intptr_t handle, dsAudioInput_t aInput, int volume)
{
        hal_dbg("Invoked.\n");
        if (false == _bIsAudioInitialized)
        {
                return dsERR_NOT_INITIALIZED;
        }
        if (!dsAudioIsValidHandle(handle) || volume < 0 || volume > 100)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
