#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>
#include "dsTypes.h"

#include "dsError.h"
#include "dsCompositeInTypes.h"
#include "dsCompositeIn.h"
#include "dshalLogger.h"

/**
 * @brief Initializes the underlying COMPOSITE Input sub-system.
 *
 * This function must initialize the COMPOSITE Input module and any associated data
 * structures.
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_ALREADY_INITIALIZED      - Module is already initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @see dsCompositeInTerm()
 */

dsError_t dsCompositeInInit(void)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Terminates the underlying COMPOSITE Input sub-system.
 *
 * This function must terminate the COMPOSITE Input module and any associated data
 * structures.
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 *
 * @see dsCompositeInInit()
 */

dsError_t dsCompositeInTerm(void)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the number of COMPOSITE Input ports on the specific platform.
 *
 * This function is used to get the number of COMPOSITE Input ports on the specific platform.
 *
 * @param[out] pNumberOfInputs   - number of COMPOSITE Input ports. Min 0. Max please refer ::dsCompositeInPort_t
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 */

dsError_t dsCompositeInGetNumberOfInputs(uint8_t *pNumberOfInputs)
{
        hal_info("invoked.\n");
        if (pNumberOfInputs == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Gets the COMPOSITE Input Status.
 *
 * This function is used to get the status of all COMPOSITE Input Status.
 *
 * @param [out] pStatus - status of compositeIn ports. Please refer ::dsCompositeInStatus_t
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 */
dsError_t dsCompositeInGetStatus(dsCompositeInStatus_t *pStatus)
{
        hal_info("invoked.\n");
        if (pStatus == NULL)
        {
                return dsERR_INVALID_PARAM;
        }
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Sets the COMPOSITE Input port as active available for presentation.
 *
 * This function is used to set the COMPOSITE Input port for presentation.
 *
 * @param[in] Port  - COMPOSITE Input port to be presented. Please refer ::dsCompositeInPort_t
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid(port is not present or index is out of range)
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @note When a port is selected that port should be set as activePort in ::dsCompositeInStatus_t.
 *              Also, if there is a signal (ie isPortConnected[that port ID] is true), once active, isPresented should be set to true as well.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 */
dsError_t dsCompositeInSelectPort(dsCompositeInPort_t Port)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Scales the COMPOSITE In video
 * This function scales the COMPOSITE input video. The width and height, based on the x, y coordinates,
 *      cannot exceed that of the current resolution of the device.
 *      e.g.  x(in pixels)+width cannot be greater then the width of the resolution.
 *      The current resolution will return by ::dsGetResolution()
 *
 * @param[in] x         - x coordinate for the video. Min of 0. Max is based on the current resolution
 * @param[in] y         - y coordinate for the video. Min of 0. Max is based on the current resolution
 * @param[in] width     - width of the video. Min of 0. Max is based on the current resolution
 * @param[in] height    - height of the video. Min of 0. Max is based on the current resolution
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit(), dsCompositeInSelectPort() should be called before calling this API.
 */

dsError_t dsCompositeInScaleVideo(int32_t x, int32_t y, int32_t width, int32_t height)
{
        hal_info("invoked.\n");
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

/**
 * @brief Registers the COMPOSITE Input hot plug event.
 *
 * This function is used to register for the COMPOSITE Input hot plug event.
 *
 * @param[in] CBFunc    - COMPOSITE Input hot plug callback function. Please refer ::dsCompositeInConnectCB_t
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 *
 * @see dsCompositeInConnectCB_t()
 */
dsError_t dsCompositeInRegisterConnectCB(dsCompositeInConnectCB_t CBFunc)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Registers the Composite Input Signal Change event.
 *
 * This function is used to register for the Composite Input Signal Change event.
 *
 * @param[in] CBFunc    - Composite Input Signal change callback function. Please refer ::dsCompositeInSignalChangeCB_t
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 *
 * @see dsCompositeInSignalChangeCB_t()
 */
dsError_t dsCompositeInRegisterSignalChangeCB(dsCompositeInSignalChangeCB_t CBFunc)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}

/**
 * @brief Registers the Composite Input Status Change event.
 *
 * This function is used to register for the Composite Input Status Change event.
 *
 * @param[in] CBFunc    - Composite Input Status change callback function. Please refer ::dsCompositeInStatusChangeCB_t
 *
 *
 * @return dsError_t                      - Status
 * @retval dsERR_NONE                     - Success
 * @retval dsERR_INVALID_PARAM            - Parameter passed to this function is invalid
 * @retval dsERR_NOT_INITIALIZED          - Module is not initialised
 * @retval dsERR_OPERATION_NOT_SUPPORTED  - The attempted operation is not supported; e.g: source devices
 * @retval dsERR_GENERAL                  - Underlying undefined platform error
 *
 * @warning  This API is Not thread safe.
 *
 * @pre  dsCompositeInInit() should be called before calling this API.
 *
 * @see dsCompositeInStatusChangeCB_t()
 */

dsError_t dsCompositeInRegisterStatusChangeCB(dsCompositeInStatusChangeCB_t CBFunc)
{
        hal_info("invoked.\n");
        return dsERR_OPERATION_NOT_SUPPORTED;
}
