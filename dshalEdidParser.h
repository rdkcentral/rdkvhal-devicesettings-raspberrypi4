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

#ifndef __DSHAL_EDID_PARSER_H__
#define __DSHAL_EDID_PARSER_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dsError.h"

#define DSHAL_EDID_BLOCK_SIZE                    128
#define DSHAL_EDID_NUM_EXTENSIONS_OFFSET          126

#define DSHAL_EDID_CTA_EXTENSION_TAG              0x02
#define DSHAL_EDID_CTA_DTD_OFFSET_INDEX           2
#define DSHAL_EDID_CTA_DATA_BLOCK_COLLECTION_START 4
#define DSHAL_EDID_CTA_MAX_OFFSET                 (DSHAL_EDID_BLOCK_SIZE - 1)
#define DSHAL_EDID_CTA_DATA_BLOCK_TAG_MASK        0xE0
#define DSHAL_EDID_CTA_DATA_BLOCK_LEN_MASK        0x1F

#define DSHAL_EDID_CTA_DATA_BLOCK_TAG_AUDIO      0x01
#define DSHAL_EDID_CTA_DATA_BLOCK_TAG_VIDEO      0x02
#define DSHAL_EDID_CTA_VENDOR_SPECIFIC_TAG       0x03
#define DSHAL_EDID_CTA_EXTENDED_TAG              0x07

#define DSHAL_EDID_CTA_SHORT_AUDIO_DESCRIPTOR_LEN 3

#define DSHAL_EDID_EXT_TAG_HDR_STATIC_METADATA    0x06

#define DSHAL_EDID_EOTF_HDR10_BIT                 0x04
#define DSHAL_EDID_EOTF_HLG_BIT                   0x08

#define DSHAL_EDID_DOLBY_VSIF_OUI_BYTE0           0x46
#define DSHAL_EDID_DOLBY_VSIF_OUI_BYTE1           0xD0
#define DSHAL_EDID_DOLBY_VSIF_OUI_BYTE2           0x00

#define DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE0       0x8B
#define DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE1       0x84
#define DSHAL_EDID_HDR10PLUS_VSIF_OUI_BYTE2       0x90

typedef bool (*dshalEdidCtaDataBlockVisitor_t)(int tag,
        const unsigned char *data,
        int dataLen,
        void *context);

bool dshalEdidForEachCtaDataBlock(const unsigned char *edid,
        int edidLen,
        dshalEdidCtaDataBlockVisitor_t visitor,
        void *context);

#endif /* __DSHAL_EDID_PARSER_H__ */
