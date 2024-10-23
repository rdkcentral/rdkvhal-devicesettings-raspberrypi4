#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "dsTypes.h"

#include "dsError.h"
#include "dsCompositeInTypes.h"
#include "dsCompositeIn.h"

dsError_t dsCompositeInInit (void)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInTerm (void)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInGetNumberOfInputs (uint8_t *pNumberOfInputs)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInGetStatus (dsCompositeInStatus_t *pStatus)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInSelectPort (dsCompositeInPort_t Port)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInScaleVideo (int32_t x, int32_t y, int32_t width, int32_t height)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInRegisterConnectCB (dsCompositeInConnectCB_t CBFunc)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInRegisterSignalChangeCB (dsCompositeInSignalChangeCB_t CBFunc)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
dsError_t dsCompositeInRegisterStatusChangeCB (dsCompositeInStatusChangeCB_t CBFunc)
{
    dsError_t ret = dsERR_NONE;
    return ret;
}
