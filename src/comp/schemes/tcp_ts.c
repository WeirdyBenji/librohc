/*
 * Copyright 2015 Didier Barvaux
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

/**
 * @file   /comp/schemes/tcp_ts.c
 * @brief  Handle encoding of TCP TimeStamp (TS) option
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "tcp_ts.h"
#include "sdvl.h"

#include <string.h>


/**
 * @brief Compress the TimeStamp option value
 *
 * See RFC4996 page 65
 *
 * @param context         The compression context
 * @param timestamp       The timestamp value to compress
 * @param bytes_nr        The number of bytes to use for encoding
 * @param[out] rohc_data  The ROHC packet being built
 * @param rohc_max_len    The max remaining length in the ROHC buffer
 * @param[out] rohc_len   The length appended in the ROHC buffer
 * @return                true if compression was successful, false otherwise
 */
bool c_tcp_ts_lsb_code(const struct rohc_comp_ctxt *const context,
                       const uint32_t timestamp,
                       const uint8_t bytes_nr,
                       uint8_t *const rohc_data,
                       const size_t rohc_max_len,
                       size_t *const rohc_len)
{
	const size_t encoded_len = bytes_nr;

	if(rohc_max_len < encoded_len)
	{
		rohc_comp_warn(context, "ROHC buffer too short for encoding the TCP TS "
		               "option: %zu byte(s) required but only %zu byte(s) "
		               "available", encoded_len, rohc_max_len);
		goto error;
	}

	if(bytes_nr == 1)
	{
		/* encoding on 1 byte with discriminator '0' */
		rohc_data[0] = timestamp & 0x7F;
		rohc_comp_debug(context, "encode timestamp = 0x%08x on 1 byte: 0x%02x",
		                timestamp, rohc_data[0]);
	}
	else if(bytes_nr == 2)
	{
		/* encoding on 2 bytes with discriminator '10' */
		rohc_data[0] = 0x80 | ((timestamp >> 8) & 0x3F);
		rohc_data[1] = timestamp;
		rohc_comp_debug(context, "encode timestamp = 0x%08x on 2 bytes: 0x%02x "
		                "0x%02x", timestamp, rohc_data[0], rohc_data[1]);
	}
	else if(bytes_nr == 3)
	{
		/* encoding on 3 bytes with discriminator '110' */
		rohc_data[0] = 0xC0 | ((timestamp >> 16) & 0x1F);
		rohc_data[1] = timestamp >> 8;
		rohc_data[2] = timestamp;
		rohc_comp_debug(context, "encode timestamp = 0x%08x on 3 bytes: 0x%02x "
		                "0x%02x 0x%02x", timestamp, rohc_data[0], rohc_data[1],
		                rohc_data[2]);
	}
	else if(bytes_nr == 4)
	{
		/* encoding on 4 bytes with discriminator '111' */
		rohc_data[0] = 0xE0 | ((timestamp >> 24) & 0x1F);
		rohc_data[1] = timestamp >> 16;
		rohc_data[2] = timestamp >> 8;
		rohc_data[3] = timestamp;
		rohc_comp_debug(context, "encode timestamp = 0x%08x on 4 bytes: 0x%02x "
		                "0x%02x 0x%02x 0x%02x", timestamp, rohc_data[0], rohc_data[1],
		                rohc_data[2], rohc_data[3]);
	}
	else
	{
		rohc_comp_warn(context, "failed to compress timestamp 0x%08x: more "
		               "than 29 bits required", timestamp);
		goto error;
	}

	*rohc_len = encoded_len;

	return true;

error:
	return false;
}

