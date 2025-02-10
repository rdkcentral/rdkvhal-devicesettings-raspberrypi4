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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include "dsFPD.h"
#include "dsFPDTypes.h"
#include "dshalLogger.h"

void setValue (int pin, int value);

char fName[128] = {0};
FILE *fd;

int LED_RED = 9;
int LED_YELLOW = 10;
int LED_GREEN = 11;

void exportPins(int pin)
{
    if ((fd = fopen("/sys/class/gpio/export", "w")) == NULL) {
        hal_err("fopen error /sys/class/gpio/export for pin(%d): %s\n", pin, strerror(errno));
        exit (1);
    }
    fprintf(fd, "%d\n", pin);
    fclose(fd);
}

void setDirection(int pin)
{
    snprintf(fName, sizeof(fName), "/sys/class/gpio/gpio%d/direction", pin);
    if ((fd = fopen (fName, "w")) == NULL) {
        hal_err("fopen error for setting direction - pin(%d): %s\n", pin, strerror(errno));
        exit(1);
    }
    fprintf(fd, "out\n") ;
    fclose(fd);
}

void setValue(int pin, int value)
{
    snprintf(fName, sizeof(fName), "/sys/class/gpio/gpio%d/value", pin);
    if ((fd = fopen (fName, "w")) == NULL) {
        hal_err("fopen error for setting value - pin(%d): %s\n", pin, strerror(errno));
        exit (1);
    }
    fprintf(fd, "%d\n", value);
    fclose(fd);
}

/**
 * @brief Initializes the Front Panel Display (FPD) sub-module of Device Settings HAL
 *
 * This function allocates required resources for Front Panel and is required to be called before the other APIs in this module.
 *
 *
 * @return dsError_t                  -  Status
 * @retval dsERR_NONE                 -  Success
 * @retval dsERR_ALREADY_INITIALIZED  -  Function is already initialized
 * @retval dsERR_GENERAL              -  Underlying undefined platform error
 *
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPTerm()
 *
 */
dsError_t dsFPInit(void)
{
    hal_info("invoked.\n");
    // These changes were added for Traffic light LED support in RPI3. Not relevant otherwise.
#if 0
    exportPins (LED_RED);
    exportPins (LED_YELLOW);
    exportPins (LED_GREEN);

    setDirection (LED_RED);
    setDirection (LED_YELLOW);
    setDirection (LED_GREEN);
#endif
    // Should not return error, as this is not a critical operation.
    return dsERR_NONE;
}

/**
 * @brief Terminates the the Front Panel Display sub-module
 *
 * This function resets any data structures used within Front Panel sub-module,
 * and releases all the resources allocated during the init function.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning This API is Not thread safe
 *
 * @see dsFPInit()
 *
 */
dsError_t dsFPTerm(void)
{
    hal_info("invoked.\n");
    return dsERR_NONE;
}

/**
 * @brief Sets blink pattern of specified Front Panel Display LED
 *
 * This function is used to set the individual discrete LED to blink for a specified number of iterations with blink interval.
 * This function must return dsERR_OPERATION_NOT_SUPPORTED if FP State is "OFF".
 *
 * @param[in] eIndicator        -  FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] uBlinkDuration    -  Blink interval. The time in ms the text display will remain ON
 *                                   during one blink iteration.
 * @param[in] uBlinkIterations  -  The number of iterations per minute data will blink
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPBlink(dsFPDIndicator_t eIndicator, unsigned int uBlinkDuration, unsigned int uBlinkIterations)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || uBlinkDuration == 0 || uBlinkIterations == 0) {
        hal_err("Invalid parameter, eIndicator: %d, uBlinkDuration: %d, uBlinkIterations: %d\n",
                eIndicator, uBlinkDuration, uBlinkIterations);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the brightness level of specified Front Panel Display LED
 *
 * This function will set the brightness of the specified discrete LED on the Front
 * Panel Display to the specified brightness level. This function must return dsERR_OPERATION_NOT_SUPPORTED
 * if the FP State is "OFF".
 *
 * @param[in] eIndicator  - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] eBrightness - The brightness value(0 to 100) for the specified indicator.
 *                            Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 c
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPBrightness()
 *
 */
dsError_t dsSetFPBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t eBrightness)
{
    // These changes were added for Traffic light LED support in RPI3. Not relevant otherwise.
#if 0
    int gpio_pin = LED_RED;

    if (eIndicator == dsFPD_INDICATOR_POWER)
        gpio_pin = LED_RED;
    else if (eIndicator == dsFPD_INDICATOR_REMOTE)
        gpio_pin = LED_YELLOW;
    else if (eIndicator == dsFPD_INDICATOR_MESSAGE)
        gpio_pin = LED_GREEN;

    setValue (gpio_pin, eBrightness);
#endif
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX || eBrightness > 100) {
        hal_err("Invalid parameter, eIndicator: %d, eBrightness: %d.\n", eIndicator, eBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the brightness level of specified Front Panel Display LED
 *
 * This function returns the brightness level of the specified discrete LED on the Front
 * Panel. This function must return dsERR_OPERATION_NOT_SUPPORTED if FP State is "OFF".
 *
 * @param[in]  eIndicator  - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] pBrightness - current brightness value(0 to 100) of the specified indicator
 *                             Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsSetFPBrightness()
 *
 */
dsError_t dsGetFPBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || pBrightness == NULL) {
        hal_err("Invalid parameter, eIndicator: %d, pBrightness: %p.\n", eIndicator, pBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the color of specified Front Panel Display LED
 *
 * This function sets the color of the specified Front Panel Indicator LED, if the
 * indicator supports it (i.e. is multi-colored). It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in] eIndicator    - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] eColor        - The color index for the specified indicator. Please refer ::dsFPDColor_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPColor()
 *
 */
dsError_t dsSetFPColor(dsFPDIndicator_t eIndicator, dsFPDColor_t eColor)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || eColor >= dsFPD_COLOR_MAX) {
        hal_err("Invalid parameter, eIndicator: %d, eColor: %d.\n", eIndicator, eColor);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the time on 7-Segment Front Panel Display LEDs
 *
 * This function sets the 7-segment display LEDs to show the time in specified format.
 * The format (12/24-hour) has to be specified. If there are no 7-Segment display LEDs present on the
 * device or if the FP State is "OFF" then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * It must return dsERR_INVALID_PARAM if the format and hours values do not agree,
 * or if the hours/minutes are invalid.
 * The FP Display Mode must be dsFPD_MODE_CLOCK/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eTimeFormat   - Time format (12 or 24 hrs). Please refer ::dsFPDTimeFormat_t
 * @param[in] uHour         - Hour information
 * @param[in] uMinutes      - Minutes information
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTimeFormat()
 *
 */
dsError_t dsSetFPTime(dsFPDTimeFormat_t eTimeFormat, const unsigned int uHour, const unsigned int uMinutes)
{
    hal_info("invoked.\n");
    if (eTimeFormat != dsFPD_TIME_12_HOUR && eTimeFormat != dsFPD_TIME_24_HOUR) {
        hal_err("Invalid parameter, eTimeFormat: %d (HH:MM %d:%d).\n", eTimeFormat, uHour, uMinutes);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Displays the specified text on 7-segment Front Panel Display LEDs
 *
 * This function is used to set the 7-segment display LEDs to show the given text.
 * If there are no 7-Segment display LEDs present on the device or if the FP State is "OFF",
 * then dsERR_OPERATION_NOT_SUPPORTED must be returned. Please refer ::dsFPDState_t.
 * The FP Display Mode must be dsFPD_MODE_TEXT/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @param[in] pText - Text to be displayed. Maximum length of Text is 10 characters.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTextBrightness()
 *
 */
dsError_t dsSetFPText(const char* pText)
{
    hal_info("invoked.\n");
    if (pText == NULL || strlen(pText) > 10) {
        hal_err("Invalid parameter, pText: %p or length %d.\n", pText, (pText == NULL) ? 0 : strlen(pText));
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables Text Scrolling on 7-segment Front Panel Display LEDs
 *
 * This function scrolls the text in the 7-segment display LEDs for the given number of iterations.
 * If there are no 7-segment display LEDs present or if the FP State is "OFF" then
 * dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * Horizontal and Vertical scroll cannot work at the same time. If both values are non-zero values
 * it should return dsERR_OPERATION_NOT_SUPPORTED.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] uScrollHoldOnDur       - Duration in ms to hold each char before scrolling to the next position
 *                                       during one scroll iteration
 * @param[in] uHorzScrollIterations  - Number of iterations to scroll horizontally
 * @param[in] uVertScrollIterations  - Number of iterations to scroll vertically
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPScroll(unsigned int uScrollHoldOnDur, unsigned int uHorzScrollIterations, unsigned int uVertScrollIterations)
{
    hal_info("invoked.\n");
    if (uScrollHoldOnDur == 0 || (uHorzScrollIterations == 0 && uVertScrollIterations == 0)) {
        hal_err("Invalid parameter, uScrollHoldOnDur: %d, uHorzScrollIterations: %d, uVertScrollIterations: %d.\n",
                uScrollHoldOnDur, uHorzScrollIterations, uVertScrollIterations);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the brightness level of 7-segment Front Panel Display LEDs
 *
 * This function will set the brightness of the specified 7-segment display LEDs on the Front
 * Panel Display to the specified brightness level. If there are no 7-Segment display LEDs present
 * on the device or if the FP State is "OFF" then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * The FP Display Mode must be dsFPD_MODE_TEXT/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eIndicator    - FPD Text indicator index. Please refer ::dsFPDTextDisplay_t
 * @param[in] eBrightness   - The brightness value for the specified indicator. Valid range is from 0 to 100
 *                              Please refer ::dsFPDBrightness_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTextBrightness(), dsSetFPText()
 *
 */
dsError_t dsSetFPTextBrightness(dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t eBrightness)
{
    hal_info("invoked.\n");
    if (!dsFPDTextDisplay_isValid(eIndicator) || eBrightness > 100) {
        hal_err("Invalid parameter, eIndicator: %d, eBrightness: %d.\n", eIndicator, eBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the brightness of 7-segment Front Panel Display LEDs
 *
 * This function will get the brightness of the specified 7-segment display LEDs on the Front
 * Panel Text Display. If there are no 7-segment display LEDs present or if the FP State is "OFF"
 * then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * The FP Display Mode must be dsFPD_MODE_CLOCK/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *  *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eIndicator    - FPD Text indicator index. Please refer ::dsFPDTextDisplay_t
 * @param[out] eBrightness  - Brightness value. Valid range is from 0 to 100. Please refer ::dsFPDBrightness_t.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTextBrightness()
 *
 */
dsError_t dsGetFPTextBrightnes(dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    hal_info("invoked.\n");
    if (!dsFPDTextDisplay_isValid(eIndicator) || pBrightness == NULL) {
        hal_err("Invalid parameter, eIndicator: %d, pBrightness: %p.\n", eIndicator, pBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Enables/Disables the clock display of Front Panel Display LEDs
 *
 * This function will enable or disable displaying of clock. It will return dsERR_OPERATION_NOT_SUPPORTED
 * if Clock display is not available
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template_template file.
 *
 * @param[in] enable    - Indicates the clock to be enabled or disabled.
 *                          1 if enabled, 0 if disabled.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t / If Clock display is not available
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsFPEnableCLockDisplay(int enable)
{
    hal_info("invoked.\n");
    if (enable != 0 && enable != 1) {
        hal_err("Invalid parameter, enable: %d.\n", enable);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the indicator state of specified discrete Front Panel Display LED
 *
 * It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in] eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[in] state      - Indicates the state of the indicator to be set. Please refer ::dsFPDState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPState()
 *
 */
dsError_t dsSetFPState(dsFPDIndicator_t eIndicator, dsFPDState_t state)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || state < dsFPD_STATE_OFF || state >= dsFPD_STATE_MAX) {
        hal_err("Invalid parameter, eIndicator: %d, state: %d.\n", eIndicator, state);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the brightness of 7-segment Front Panel Display LEDs
 *
 * This function will get the brightness of the specified 7-segment display LEDs on the Front
 * Panel Text Display. If there are no 7-segment display LEDs present or if the FP State is "OFF"
 * then dsERR_OPERATION_NOT_SUPPORTED must be returned.
 * The FP Display Mode must be dsFPD_MODE_CLOCK/dsFPD_MODE_ANY. Please refer ::dsFPDMode_t
 *  *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eIndicator    - FPD Text indicator index. Please refer ::dsFPDTextDisplay_t
 * @param[out] eBrightness  - Brightness value. Valid range is from 0 to 100. Please refer ::dsFPDBrightness_t.
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTextBrightness()
 *
 */
dsError_t dsGetFPTextBrightness(dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    hal_info("invoked.\n");
    if (!dsFPDTextDisplay_isValid(eIndicator) || pBrightness == NULL) {
        hal_err("Invalid parameter, eIndicator: %d, pBrightness: %p.\n", eIndicator, pBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

dsError_t dsSetFPDBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t eBrightness, bool toPersist)
{
    hal_info("invoked with toPersist %d.\n", toPersist);
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || eBrightness > dsFPD_BRIGHTNESS_MAX)
    {
        hal_err("Invalid parameter, eIndicator: %d, eBrightness: %d.\n", eIndicator, eBrightness);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsSetFPDColor(dsFPDIndicator_t eIndicator, dsFPDColor_t eColor, bool toPersist)
{
    hal_info("invoked with toPersist %d.\n", toPersist);
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || (eColor != dsFPD_COLOR_BLUE && eColor != dsFPD_COLOR_GREEN && eColor != dsFPD_COLOR_RED
                && eColor != dsFPD_COLOR_YELLOW && eColor != dsFPD_COLOR_ORANGE && eColor != dsFPD_COLOR_WHITE))
    {
        hal_err("Invalid parameter, eIndicator: %d, eColor: %d.\n", eIndicator, eColor);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief  Gets the color of specified Front Panel Display LED
 *
 * This function gets the color of the specified Front Panel Indicator LED, if the
 * indicator supports it (i.e. is multi-colored). It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in] eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] pColor    - current color value of the specified indicator. Please refer ::dsFPDColor_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPColor()
 *
 */
dsError_t dsGetFPColor(dsFPDIndicator_t eIndicator, dsFPDColor_t *pColor)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || pColor == NULL) {
        hal_err("Invalid parameter, eIndicator: %d, pColor: %p.\n", eIndicator, pColor);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the indicator state of specified discrete Front Panel Display LED
 *
 * It must return
 * dsERR_OPERATION_NOT_SUPPORTED if the indicator is single-colored or if the FP State is "OFF".
 *
 * @param[in]  eIndicator - FPD indicator index. Please refer ::dsFPDIndicator_t
 * @param[out] state      - current state of the specified indicator. Please refer ::dsFPDState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPState()
 *
 */
dsError_t dsGetFPState(dsFPDIndicator_t eIndicator, dsFPDState_t* state)
{
    hal_info("invoked.\n");
    if (eIndicator < dsFPD_INDICATOR_MESSAGE || eIndicator >= dsFPD_INDICATOR_MAX
            || state == NULL) {
        hal_err("Invalid parameter, eIndicator: %d, state: %p.\n", eIndicator, state);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the current time format on the 7-segment Front Panel Display LEDs
 *
 * This function sets the 7-segment display LEDs to show the
 * specified time in specified format. It must return dsERR_OPERATION_NOT_SUPPORTED
 * if the underlying hardware does not have support for clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eTimeFormat   -  Indicates the time format (12 hour or 24 hour).
 *                               Please refer ::dsFPDTimeFormat_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @note The time display should also change according to the new format set
 *
 * @warning  This API is Not thread safe
 *
 * @see dsGetFPTimeFormat()
 *
 */
dsError_t dsSetFPTimeFormat(dsFPDTimeFormat_t eTimeFormat)
{
    hal_info("invoked.\n");
    if (eTimeFormat != dsFPD_TIME_12_HOUR && eTimeFormat != dsFPD_TIME_24_HOUR) {
        hal_err("Invalid parameter, eTimeFormat: %d.\n", eTimeFormat);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the current time format on the 7-segment Front Panel Display LEDs
 *
 * This function gets the current time format set on 7-segment display LEDs panel.
 * It must return dsERR_OPERATION_NOT_SUPPORTED if the underlying hardware does not
 * have support for clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[out] pTimeFormat      - Current time format value (12 hour or 24 hour).
 *                                  Please refer ::dsFPDTimeFormat_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsSetFPTimeFormat()
 *
 */
dsError_t dsGetFPTimeFormat(dsFPDTimeFormat_t *pTimeFormat)
{
    hal_info("invoked.\n");
    if (pTimeFormat == NULL) {
        hal_err("Invalid parameter, pTimeFormat: %p.\n", pTimeFormat);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the display mode of the Front Panel Display LEDs
 *
 * This function sets the display mode (clock or text or both) for FPD.
 * It must return dsERR_OPERATION_NOT_SUPPORTED if the underlying hardware does not
 * have support for Text or Clock.
 *
 * @note Whether this device has a 7-Segment display LEDs should be within the dsFPDSettings_template file.
 *
 * @param[in] eMode - Indicates the mode. Please refer ::dsFPDMode_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported/FP State is "OFF". Please refer ::dsFPDState_t
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called and FP State must be "ON" before calling this API
 *
 * @warning  This API is Not thread safe
 *
 */
dsError_t dsSetFPDMode(dsFPDMode_t eMode)
{
    hal_info("invoked.\n");
    if (eMode != dsFPD_MODE_CLOCK && eMode != dsFPD_MODE_TEXT && eMode != dsFPD_MODE_ANY) {
        hal_err("Invalid parameter, eMode: %d.\n", eMode);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the current power Front Panel Display LED state
 *
 * This function gets the current power LED state
 *
 * @param[out] state - Current LED state. Please refer ::dsFPDLedState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPSetLEDState()
 */
dsError_t dsFPGetLEDState(dsFPDLedState_t* state)
{
    hal_info("invoked.\n");
    if (state == NULL) {
        hal_err("Invalid parameter, state: %p.\n", state);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the current power Front Panel Display LED state
 *
 * This function sets the current power LED state
 *
 * @param[in] state - LED state. Please refer ::dsFPDLedState_t
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 *
 * @see dsFPGetLEDState()
 */
dsError_t dsFPSetLEDState(dsFPDLedState_t state)
{
    hal_info("invoked.\n");
    if (state < dsFPD_LED_DEVICE_NONE || state >= dsFPD_LED_DEVICE_MAX) {
        hal_err("Invalid parameter, state: %d.\n", state);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the supported led states
 *
 * This function gets the supported led states
 *
 * @param[out] states - The bitwise value of all supported led states by the platform. refer ::dsFPDLedState_t
 *      e.g. *states = ((1<<dsFPD_LED_DEVICE_ACTIVE) | (1<<dsFPD_LED_DEVICE_STANDBY))
 *
 * @return dsError_t                      -  Status
 * @retval dsERR_NONE                     -  Success
 * @retval dsERR_NOT_INITIALIZED          -  Module is not initialised
 * @retval dsERR_INVALID_PARAM            -  Parameter passed to this function is invalid
 * @retval dsERR_OPERATION_NOT_SUPPORTED  -  The attempted operation is not supported
 * @retval dsERR_GENERAL                  -  Underlying undefined platform error
 *
 * @pre dsFPInit() must be called before calling this API
 *
 * @warning  This API is Not thread safe
 */
dsError_t dsFPGetSupportedLEDStates(unsigned int* states)
{
    hal_info("invoked.\n");
    if (states == NULL) {
        hal_err("Invalid parameter, states: %p.\n", states);
        return dsERR_INVALID_PARAM;
    }
    return dsERR_OPERATION_NOT_SUPPORTED;
}
