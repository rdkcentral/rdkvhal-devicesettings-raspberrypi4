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
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInTerm (void)
{
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInGetNumberOfInputs (uint8_t *pNumberOfInputs)
{
        if(pNumberOfInputs == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInGetStatus (dsCompositeInStatus_t *pStatus)
{
        if(pStatus == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInSelectPort (dsCompositeInPort_t Port)
{
                return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInScaleVideo (int32_t x, int32_t y, int32_t width, int32_t height)
{
        if (width <= 0 || height <= 0)
        {
                return dsERR_INVALID_PARAM; // Width and height must be positive
        }
        if (x < 0 || y < 0) 
        {
                return dsERR_INVALID_PARAM; // x and y must be non-negative
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInRegisterConnectCB (dsCompositeInConnectCB_t CBFunc)
{
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInRegisterSignalChangeCB (dsCompositeInSignalChangeCB_t CBFunc)
{
        return dsERR_OPERATION_NOT_SUPPORTED;
}
dsError_t dsCompositeInRegisterStatusChangeCB (dsCompositeInStatusChangeCB_t CBFunc)
{
        return dsERR_OPERATION_NOT_SUPPORTED;
}
