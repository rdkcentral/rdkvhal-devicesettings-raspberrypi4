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
#include <errno.h>
#include "dsFPD.h"

void setValue (int pin, int value);

char fName [128] ;
FILE *fd ;

int LED_RED = 9;
int LED_YELLOW = 10;
int LED_GREEN = 11;

void exportPins (int pin)
{
    if ((fd = fopen ("/sys/class/gpio/export", "w")) == NULL)
    {
        fprintf (stderr, "%d: Unable to open GPIO export interface: %s\n", pin, strerror (errno)) ;
        exit (1) ;
    }

    fprintf (fd, "%d\n", pin) ;
    fclose (fd) ;
}

void setDirection (int pin)
{
    sprintf (fName, "/sys/class/gpio/gpio%d/direction", pin) ;
    if ((fd = fopen (fName, "w")) == NULL)
    {
        fprintf (stderr, "Unable to open GPIO direction interface for pin %d: %s\n", pin, strerror (errno)) ;
        exit (1) ;
    }
    fprintf (fd, "out\n") ;
    fclose (fd) ;
}

void setValue (int pin, int value)
{
    sprintf (fName, "/sys/class/gpio/gpio%d/value", pin) ;
    if ((fd = fopen (fName, "w")) == NULL)
    {
        fprintf (stderr, "Unable to open GPIO direction interface for pin %d: %s\n", pin, strerror (errno)) ;
        exit (1) ;
    }
    fprintf (fd, "%d\n", value) ;
    fclose (fd) ;
}

dsError_t dsFPInit(void)
{
// These changes were added for Traffic light LED support in RPI3. Not relevant otherwise.
#if 0
    exportPins (LED_RED);
    exportPins (LED_YELLOW);
    exportPins (LED_GREEN);

    setDirection (LED_RED);
    setDirection (LED_YELLOW);
    setDirection (LED_GREEN);
#endif
    return dsERR_NONE;
}

dsError_t dsFPTerm(void)
{
    return dsERR_NONE;
}

dsError_t dsSetFPBlink (dsFPDIndicator_t eIndicator, unsigned int uBlinkDuration, unsigned int uBlinkIterations)
{
    return dsERR_NONE;
}

dsError_t dsSetFPBrightness (dsFPDIndicator_t eIndicator, dsFPDBrightness_t eBrightness)
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
    return dsERR_NONE;
}

dsError_t dsGetFPBrightness (dsFPDIndicator_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    return dsERR_NONE;
}

dsError_t dsSetFPColor (dsFPDIndicator_t eIndicator, dsFPDColor_t eColor)
{
    return dsERR_NONE;
}

dsError_t dsSetFPTime (dsFPDTimeFormat_t eTimeFormat, const unsigned int uHour, const unsigned int uMinutes)
{
    return dsERR_NONE;
}

dsError_t dsSetFPText(const char* pText)
{
    return dsERR_NONE;
}

dsError_t dsSetFPScroll (unsigned int uScrollHoldOnDur, unsigned int uHorzScrollIterations, unsigned int uVertScrollIterations)
{
    return dsERR_NONE;
}


dsError_t dsSetFPTextBrightness (dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t eBrightness)
{
    return dsERR_NONE;
}
dsError_t dsGetFPTextBrightnes  (dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    return dsERR_NONE;
}

dsError_t dsFPEnableCLockDisplay (int enable)
{
    return dsERR_NONE;
}

dsError_t dsSetFPState (dsFPDIndicator_t eIndicator, dsFPDState_t state)
{
    return dsERR_NONE;
}
dsError_t dsGetFPTextBrightness (dsFPDTextDisplay_t eIndicator, dsFPDBrightness_t *pBrightness)
{
    return dsERR_NONE;
}

dsError_t dsSetFPDBrightness(dsFPDIndicator_t eIndicator, dsFPDBrightness_t eBrightness,bool toPersist)
{
    return dsERR_GENERAL;
}
dsError_t dsSetFPDColor (dsFPDIndicator_t eIndicator, dsFPDColor_t eColor,bool toPersist)
{
    return dsERR_GENERAL;
}
dsError_t dsGetFPColor (dsFPDIndicator_t eIndicator, dsFPDColor_t *pColor)
{
    return dsERR_GENERAL;
}
dsError_t dsGetFPState (dsFPDIndicator_t eIndicator, dsFPDState_t* state)
{
    return dsERR_NONE;
}
dsError_t dsSetFPTimeFormat (dsFPDTimeFormat_t eTimeFormat)
{
    return dsERR_NONE;
}
dsError_t dsGetFPTimeFormat (dsFPDTimeFormat_t *pTimeFormat)
{
    return dsERR_NONE;
}
dsError_t dsSetFPDMode (dsFPDMode_t eMode)
{
    return dsERR_NONE;
}
dsError_t dsFPGetLEDState (dsFPDLedState_t* state)
{
    return dsERR_NONE;
}
dsError_t dsFPSetLEDState (dsFPDLedState_t state)
{
    return dsERR_NONE;
}
dsError_t dsFPGetSupportedLEDStates (unsigned int* states)
{
    return dsERR_NONE;
}


