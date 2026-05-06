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
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "dshalEdidParser.h"

#include <stdint.h>

bool dshalEdidForEachCtaDataBlock(const unsigned char *edid,
        int edidLen,
        dshalEdidCtaDataBlockVisitor_t visitor,
        void *context)
{
    if (edid == 0 || edidLen < DSHAL_EDID_BLOCK_SIZE || visitor == 0) {
        return false;
    }

    int num_ext = edid[DSHAL_EDID_NUM_EXTENSIONS_OFFSET] & 0xFF;
    for (int ext = 0; ext < num_ext; ext++) {
        int blk_start = (ext + 1) * DSHAL_EDID_BLOCK_SIZE;
        if (blk_start + (DSHAL_EDID_BLOCK_SIZE - 1) >= edidLen) {
            break;
        }

        const unsigned char *blk = edid + blk_start;
        if (blk[0] != DSHAL_EDID_CTA_EXTENSION_TAG) {
            continue;
        }

        int dtd_offset = blk[DSHAL_EDID_CTA_DTD_OFFSET_INDEX];
        if (dtd_offset < DSHAL_EDID_CTA_DATA_BLOCK_COLLECTION_START || dtd_offset > DSHAL_EDID_CTA_MAX_OFFSET) {
            continue;
        }

        int pos = DSHAL_EDID_CTA_DATA_BLOCK_COLLECTION_START;
        while (pos < dtd_offset && pos < DSHAL_EDID_CTA_MAX_OFFSET) {
            int tag = (blk[pos] & DSHAL_EDID_CTA_DATA_BLOCK_TAG_MASK) >> 5;
            int length = blk[pos] & DSHAL_EDID_CTA_DATA_BLOCK_LEN_MASK;
            int data_start = pos + 1;
            int data_end = data_start + length;

            /* Reject malformed block headers that do not advance parsing. */
            if (data_end <= pos) {
                break;
            }

            /* Data blocks must stay inside the collection and the 128-byte CTA block. */
            if (data_end > dtd_offset || data_end > DSHAL_EDID_BLOCK_SIZE) {
                break;
            }

            if (visitor(tag, blk + data_start, length, context)) {
                return true;
            }

            pos = data_end;
        }
    }

    return true;
}
