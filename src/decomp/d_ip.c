/*
 * Copyright 2010,2012,2013,2014 Didier Barvaux
 * Copyright 2007,2008 Thales Alenia Space
 * Copyright 2007,2008,2009,2010,2012 Viveris Technologies
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
 * @file d_ip.c
 * @brief ROHC decompression context for the IP-only profile.
 * @author Didier Barvaux <didier.barvaux@toulouse.viveris.com>
 * @author Didier Barvaux <didier@barvaux.org>
 */

#include "d_ip.h"
#include "rohc_traces_internal.h"
#include "rohc_bit_ops.h"
#include "rohc_packets.h"
#include "rohc_debug.h" /* for zfree() */
#include "rohc_utils.h"
#include "rohc_decomp_detect_packet.h"

#include <assert.h>


/*
 * Private function prototypes.
 */

static bool d_ip_create(const struct rohc_decomp_ctxt *const context,
                        struct rohc_decomp_rfc3095_ctxt **const persist_ctxt,
                        struct rohc_decomp_volat_ctxt *const volat_ctxt)
	__attribute__((warn_unused_result, nonnull(1, 2, 3)));

static void d_ip_destroy(struct rohc_decomp_rfc3095_ctxt *const rfc3095_ctxt,
                         const struct rohc_decomp_volat_ctxt *const volat_ctxt)
	__attribute__((nonnull(1, 2)));


/**
 * @brief Create the IP decompression context.
 *
 * This function is one of the functions that must exist in one profile for the
 * framework to work.
 *
 * @param context            The decompression context
 * @param[out] persist_ctxt  The persistent part of the decompression context
 * @param[out] volat_ctxt    The volatile part of the decompression context
 * @return                   true if the UDP context was successfully created,
 *                           false if a problem occurred
 */
static bool d_ip_create(const struct rohc_decomp_ctxt *const context,
                        struct rohc_decomp_rfc3095_ctxt **const persist_ctxt,
                        struct rohc_decomp_volat_ctxt *const volat_ctxt)
{
	struct rohc_decomp_rfc3095_ctxt *rfc3095_ctxt;

	assert(context->decompressor != NULL);
	assert(context->profile != NULL);

	/* create the generic context */
	if(!rohc_decomp_rfc3095_create(context, persist_ctxt, volat_ctxt,
	                               context->decompressor->trace_callback,
	                               context->decompressor->trace_callback_priv,
	                               context->profile->id))
	{
		rohc_error(context->decompressor, ROHC_TRACE_DECOMP, context->profile->id,
		           "failed to create the generic decompression context");
		goto quit;
	}
	rfc3095_ctxt = *persist_ctxt;
	rfc3095_ctxt->specific = NULL;

	/* create the LSB decoding context for SN */
	rohc_lsb_init(&rfc3095_ctxt->sn_lsb_ctxt, 16);

	/* some IP-specific values and functions */
	rfc3095_ctxt->parse_dyn_next_hdr = ip_parse_dynamic_ip;
	rfc3095_ctxt->parse_ext3 = ip_parse_ext3;

	return true;

quit:
	return false;
}


/**
 * @brief Destroy the given IP-only context
 *
 * This function is one of the functions that must exist in one profile for the
 * framework to work.
 *
 * @param rfc3095_ctxt  The persistent decompression context for the RFC3095 profiles
 * @param volat_ctxt    The volatile decompression context
 */
static void d_ip_destroy(struct rohc_decomp_rfc3095_ctxt *const rfc3095_ctxt,
                         const struct rohc_decomp_volat_ctxt *const volat_ctxt)
{
	rohc_decomp_rfc3095_destroy(rfc3095_ctxt, volat_ctxt);
}


/**
 * @brief Detect the type of ROHC packet for IP-based non-RTP profiles
 *
 * @param context        The decompression context
 * @param rohc_packet    The ROHC packet
 * @param rohc_length    The length of the ROHC packet
 * @param large_cid_len  The length of the optional large CID field
 * @return               The packet type
 */
rohc_packet_t ip_detect_packet_type(const struct rohc_decomp_ctxt *const context,
                                    const uint8_t *const rohc_packet,
                                    const size_t rohc_length,
                                    const size_t large_cid_len __attribute__((unused)))
{
	rohc_packet_t type;

	/* at least one byte required to check discriminator byte in packet
	 * (already checked by rohc_decomp_find_context) */
	assert(rohc_length >= 1);

	rohc_decomp_debug(context, "try to determine the header from first byte "
	                  "0x%02x", rohc_packet[0]);

	if(rohc_decomp_packet_is_uo0(rohc_packet, rohc_length))
	{
		/* UO-0 packet */
		type = ROHC_PACKET_UO_0;
	}
	else if(rohc_decomp_packet_is_uo1(rohc_packet, rohc_length))
	{
		/* UO-1 packet */
		type = ROHC_PACKET_UO_1;
	}
	else if(rohc_decomp_packet_is_uor2(rohc_packet, rohc_length))
	{
		/* UOR-2 packet */
		type = ROHC_PACKET_UOR_2;
	}
	else if(rohc_decomp_packet_is_irdyn(rohc_packet, rohc_length))
	{
		/* IR-DYN packet */
		type = ROHC_PACKET_IR_DYN;
	}
	else if(rohc_decomp_packet_is_ir(rohc_packet, rohc_length))
	{
		/* IR packet */
		type = ROHC_PACKET_IR;
	}
	else
	{
		/* unknown packet */
		rohc_decomp_warn(context, "failed to recognize the packet type in byte "
		                 "0x%02x", rohc_packet[0]);
		type = ROHC_PACKET_UNKNOWN;
	}

	return type;
}


/**
 * @brief Parse the IP dynamic part of the ROHC packet.
 *
 * @param context      The decompression context
 * @param packet       The ROHC packet to parse
 * @param length       The length of the ROHC packet
 * @param bits         OUT: The bits extracted from the ROHC header
 * @return             The number of bytes read in the ROHC packet,
 *                     -1 in case of failure
 */
int ip_parse_dynamic_ip(const struct rohc_decomp_ctxt *const context,
                        const uint8_t *packet,
                        const size_t length,
                        struct rohc_extr_bits *const bits)
{
	size_t read = 0; /* number of bytes read from the packet */

	/* check the minimal length to decode the SN field */
	if(length < 2)
	{
		rohc_decomp_warn(context, "ROHC packet too small (len = %zu)", length);
		goto error;
	}

	/* parse 16-bit SN */
	bits->sn = rohc_ntoh16(GET_NEXT_16_BITS(packet));
	bits->sn_nr = 16;
	bits->is_sn_enc = false;
	rohc_decomp_debug(context, "SN = %u (0x%04x)", bits->sn, bits->sn);
#ifndef __clang_analyzer__ /* silent warning about dead in/decrement */
	packet += 2;
#endif
	read += 2;

	return read;

error:
	return -1;
}


/**
 * @brief Parse the extension 3 of the UOR-2 packet
 *
 * \verbatim

 Extension 3 for non-RTP profiles (5.7.5 & 5.11.4):

       0     1     2     3     4     5     6     7
    +-----+-----+-----+-----+-----+-----+-----+-----+
 1  |  1     1  |  S  |   Mode    |  I  | ip  | ip2 |
    +-----+-----+-----+-----+-----+-----+-----+-----+
 2  |            Inner IP header flags        |     |  if ip = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+
 3  |            Outer IP header flags              |  if ip2 = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+
 4  |                      SN                       |  if S = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+
    |                                               |
 5  /            Inner IP header fields             /  variable,
    |                                               |  if ip = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+
 6  |                     IP-ID                     |  2 octets, if I = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+
    |                                               |
 7  /            Outer IP header fields             /  variable,
    |                                               |  if ip2 = 1
    +-----+-----+-----+-----+-----+-----+-----+-----+

\endverbatim
 *
 * @param context           The decompression context
 * @param rohc_data         The ROHC data to parse
 * @param rohc_data_len     The length of the ROHC data to parse
 * @param packet_type       The type of ROHC packet to parse
 * @param bits              IN: the bits already found in base header
 *                          OUT: the bits found in the extension header 3
 * @return                  The data length read from the ROHC packet,
 *                          -1 in case of error
 */
int ip_parse_ext3(const struct rohc_decomp_ctxt *const context,
                  const uint8_t *const rohc_data,
                  const size_t rohc_data_len,
                  const rohc_packet_t packet_type,
                  struct rohc_extr_bits *const bits)
{
	const uint8_t *ip_flags_pos = NULL;
	const uint8_t *ip2_flags_pos = NULL;
	uint8_t S;
	uint8_t I;
	uint8_t ip;
	uint8_t ip2;
	uint16_t I_bits;
	int size;

	/* remaining ROHC data */
	const uint8_t *rohc_remain_data = rohc_data;
	size_t rohc_remain_len = rohc_data_len;

	/* sanity checks */
	assert(packet_type == ROHC_PACKET_UOR_2);

	rohc_decomp_debug(context, "decode extension 3");

	/* check the minimal length to decode the flags */
	if(rohc_remain_len < 1)
	{
		rohc_decomp_warn(context, "ROHC packet too small (len = %zu)",
		                 rohc_remain_len);
		goto error;
	}

	/* extract flags */
	S = GET_REAL(GET_BIT_5(rohc_remain_data));
	bits->mode = GET_BIT_3_4(rohc_remain_data);
	bits->mode_nr = 2;
	if(bits->mode == 0)
	{
		rohc_decomp_debug(context, "malformed ROHC packet: unexpected value zero "
		                  "for Mode bits in extension 3, value zero is reserved "
		                  "for future usage according to RFC3095");
#ifdef ROHC_RFC_STRICT_DECOMPRESSOR
		goto error;
#endif
	}
	I = GET_REAL(GET_BIT_2(rohc_remain_data));
	ip = GET_REAL(GET_BIT_1(rohc_remain_data));
	ip2 = GET_REAL(GET_BIT_0(rohc_remain_data));
	rohc_decomp_debug(context, "S = %u, mode = 0x%x, I = %u, ip = %u, "
	                  "ip2 = %u", S, bits->mode, I, ip, ip2);
	rohc_remain_data++;
	rohc_remain_len--;

	/* check the minimal length to decode the inner & outer IP header flags
	 * and the SN */
	if(rohc_remain_len < ((size_t) (ip + ip2 + S)))
	{
		rohc_decomp_warn(context, "ROHC packet too small (len = %zu)",
		                 rohc_remain_len);
		goto error;
	}

	/* remember position of inner IP header flags if present */
	if(ip)
	{
		rohc_decomp_debug(context, "inner IP header flags field is present in "
		                  "EXT-3 = 0x%02x", GET_BIT_0_7(rohc_remain_data));
		if(bits->multiple_ip)
		{
			ip2_flags_pos = rohc_remain_data;
		}
		else
		{
			ip_flags_pos = rohc_remain_data;
		}
		rohc_remain_data++;
		rohc_remain_len--;
	}

	/* remember position of outer IP header flags if present */
	if(ip2)
	{
		rohc_decomp_debug(context, "outer IP header flags field is present in "
		                  "EXT-3 = 0x%02x", GET_BIT_0_7(rohc_remain_data));
		ip_flags_pos = rohc_remain_data;
		rohc_remain_data++;
		rohc_remain_len--;
	}

	/* extract the SN if present */
	if(S)
	{
		APPEND_SN_BITS(ROHC_EXT_3, bits, GET_BIT_0_7(rohc_remain_data), 8);
		rohc_remain_data++;
		rohc_remain_len--;
	}

	/* decode the inner IP header fields (pointed by packet) according to the
	 * inner IP header flags (pointed by ip(2)_flags_pos) if present */
	if(ip)
	{
		if(bits->multiple_ip)
		{
			size = ip_parse_inner_hdr_flags_fields(context, ip2_flags_pos,
			                                       rohc_remain_data, rohc_remain_len,
			                                       &bits->inner_ip);
		}
		else
		{
			size = ip_parse_inner_hdr_flags_fields(context, ip_flags_pos,
			                                       rohc_remain_data, rohc_remain_len,
			                                       &bits->outer_ip);
		}
		if(size < 0)
		{
			rohc_decomp_warn(context, "cannot decode the inner IP header flags "
			                 "& fields");
			goto error;
		}
		rohc_remain_data += size;
		rohc_remain_len -= size;
	}

	/* skip the IP-ID if present, it will be parsed later once all RND bits
	 * have been parsed (ie. outer IP header flags), otherwise a problem
	 * may occur: if you have context(outer RND) = 1 and context(inner RND) = 0
	 * and value(outer RND) = 0 and value(inner RND) = 1, then here in the
	 * code, we have no IP header with non-random IP-ID */
	if(I)
	{
		/* check the minimal length to decode the IP-ID field */
		if(rohc_remain_len < 2)
		{
			rohc_decomp_warn(context, "ROHC packet too small (len = %zu)",
			                 rohc_remain_len);
			goto error;
		}

		/* both inner and outer IP-ID fields are 2-byte long */
		I_bits = ((rohc_remain_data[0] << 8) & 0xff00) |
		         (rohc_remain_data[1] & 0x00ff);
		rohc_remain_data += 2;
		rohc_remain_len -= 2;
	}
	else
	{
		I_bits = 0;
	}

	/* decode the outer IP header fields according to the outer IP header
	 * flags if present */
	if(ip2)
	{
		size = rfc3095_parse_outer_hdr_flags_fields(context, ip_flags_pos,
		                                            rohc_remain_data, rohc_remain_len,
		                                            &bits->outer_ip);
		if(size == -1)
		{
			rohc_decomp_warn(context, "cannot decode the outer IP header flags "
			                 "& fields");
			goto error;
		}
#ifndef __clang_analyzer__ /* silent warning about dead increment */
		rohc_remain_data += size;
#endif
		rohc_remain_len -= size;
	}

	if(I)
	{
		/* determine which IP header is the innermost IPv4 header with
		 * non-random IP-ID */
		if(bits->multiple_ip && is_ipv4_non_rnd_pkt(&bits->inner_ip))
		{
			/* inner IP header is IPv4 with non-random IP-ID */
			if(bits->inner_ip.id_nr > 0 && bits->inner_ip.id != 0)
			{
				rohc_decomp_warn(context, "IP-ID field present (I = 1) but inner "
				                 "IP-ID already updated");
#ifdef ROHC_RFC_STRICT_DECOMPRESSOR
				goto error;
#endif
			}
			bits->inner_ip.id = I_bits;
			bits->inner_ip.id_nr = 16;
			bits->inner_ip.is_id_enc = true;
			rohc_decomp_debug(context, "%zd bits of inner IP-ID in EXT3 = 0x%x",
			                  bits->inner_ip.id_nr, bits->inner_ip.id);
		}
		else if(is_ipv4_non_rnd_pkt(&bits->outer_ip))
		{
			/* inner IP header is not 'IPv4 with non-random IP-ID', but outer
			 * IP header is */
			if(bits->outer_ip.id_nr > 0 && bits->outer_ip.id != 0)
			{
				rohc_decomp_warn(context, "IP-ID field present (I = 1) but outer "
				                 "IP-ID already updated");
#ifdef ROHC_RFC_STRICT_DECOMPRESSOR
				goto error;
#endif
			}
			bits->outer_ip.id = I_bits;
			bits->outer_ip.id_nr = 16;
			bits->outer_ip.is_id_enc = true;
			rohc_decomp_debug(context, "%zd bits of outer IP-ID in EXT3 = 0x%x",
			                  bits->outer_ip.id_nr, bits->outer_ip.id);
		}
		else
		{
			rohc_decomp_warn(context, "extension 3 cannot contain IP-ID bits "
			                 "because no IP header is IPv4 with non-random IP-ID");
			goto error;
		}
	}

	return (rohc_data_len - rohc_remain_len);

error:
	return -1;
}


/**
 * @brief Parse the inner IP header flags and fields
 *
 * @param context         The decompression context
 * @param flags           The ROHC flags that indicate which IP fields are present
 *                        in the packet
 * @param fields          The ROHC packet part that contains some IP header fields
 * @param length          The length of the ROHC packet part that contains some IP
 *                        header fields
 * @param[out] bits       The bits extracted from extension 3
 * @return                The data length read from the ROHC packet,
 *                        -1 in case of error
 */
int ip_parse_inner_hdr_flags_fields(const struct rohc_decomp_ctxt *const context,
                                    const uint8_t *const flags,
                                    const uint8_t *fields,
                                    const size_t length,
                                    struct rohc_extr_ip_bits *const bits)
{
	bool reserved_flag;
	int ret;

	ret = rfc3095_parse_hdr_flags_fields(context, flags, fields, length,
	                                     &reserved_flag, bits);
	if(ret >= 0 && reserved_flag)
	{
		rohc_decomp_debug(context, "malformed ROHC header flags: reserved field "
		                  "shall be zero but it is %u", reserved_flag);
#ifdef ROHC_RFC_STRICT_DECOMPRESSOR
		return -1;
#endif
	}

	return ret;
}


/**
 * @brief Define the decompression part of the IP-only profile as described
 *        in the RFC 3843.
 */
const struct rohc_decomp_profile d_ip_profile =
{
	.id              = ROHC_PROFILE_IP, /* profile ID (see 5 in RFC 3843) */
	.msn_max_bits    = 16,
	.new_context     = (rohc_decomp_new_context_t) d_ip_create,
	.free_context    = (rohc_decomp_free_context_t) d_ip_destroy,
	.detect_pkt_type = ip_detect_packet_type,
	.parse_pkt       = (rohc_decomp_parse_pkt_t) rfc3095_decomp_parse_pkt,
	.decode_bits     = (rohc_decomp_decode_bits_t) rfc3095_decomp_decode_bits,
	.build_hdrs      = (rohc_decomp_build_hdrs_t) rfc3095_decomp_build_hdrs,
	.update_ctxt     = (rohc_decomp_update_ctxt_t) rfc3095_decomp_update_ctxt,
	.attempt_repair  = (rohc_decomp_attempt_repair_t) rfc3095_decomp_attempt_repair,
	.get_sn          = rohc_decomp_rfc3095_get_sn,
};

