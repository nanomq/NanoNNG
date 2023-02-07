//
// Copyright 2022 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "core/nng_impl.h"
#include "core/sockimpl.h"
#include "core/zmalloc.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "supplemental/mqtt/mqtt_msg.h"

#include "nng/mqtt/packet.h"
// #include <iconv.h>
#include <stdio.h>
#include <string.h>

#if defined(__APPLE__)

#include <libkern/OSByteOrder.h>
#define le64toh(x) OSSwapLittleToHostInt64(x)

#elif defined(__WINDOWS__)

#define le64toh(x) (x)

#else

#include <endian.h>

#endif // __APPLE__

struct pub_extra {
	uint8_t  qos;
	uint16_t packet_id;
	void *   msg;
};

static uint8_t  get_value_size(uint64_t value);
static uint64_t power(uint64_t x, uint32_t n);

pub_extra *
pub_extra_alloc(pub_extra *extra)
{
	return NNI_ALLOC_STRUCT(extra);
}

void
pub_extra_free(pub_extra *pub_extra)
{
	if (pub_extra) {
		NNI_FREE_STRUCT(pub_extra);
	}
}

uint8_t
pub_extra_get_qos(pub_extra *pub_extra)
{
	return pub_extra->qos;
}

void
pub_extra_set_qos(pub_extra *pub_extra, uint8_t qos)
{
	pub_extra->qos = qos;
}

uint16_t
pub_extra_get_packet_id(pub_extra *pub_extra)
{
	return pub_extra->packet_id;
}

void
pub_extra_set_packet_id(pub_extra *pub_extra, uint16_t packet_id)
{
	pub_extra->packet_id = packet_id;
}

void *
pub_extra_get_msg(pub_extra *pub_extra)
{
	return pub_extra->msg;
}

void
pub_extra_set_msg(pub_extra *pub_extra, void *msg)
{
	pub_extra->msg = msg;
}

static uint64_t
power(uint64_t x, uint32_t n)
{
	uint64_t val = 1;

	for (uint32_t i = 0; i <= n; ++i) {
		val = x * val;
	}

	return val / x;
}

/**
 * get size from value
 *
 * @param value
 * @return
 */
static uint8_t
get_value_size(uint64_t value)
{
	uint8_t  len = 1;
	uint64_t pow;
	for (int i = 1; i <= 4; ++i) {
		pow = power(0x080, i);
		if (value >= pow) {
			++len;
		} else {
			break;
		}
	}
	return len;
}

/**
 * put a value to variable byte array
 * @param dest
 * @param value
 * @return data length
 */
uint8_t
put_var_integer(uint8_t *dest, uint32_t value)
{
	uint8_t  len        = 0;
	uint32_t init_val   = 0x7F;
	uint8_t  value_size = get_value_size(value);

	for (uint32_t i = 0; i < value_size; ++i) {

		if (i > 0) {
			init_val = (init_val * 0x80) | 0xFF;
		}
		dest[i] = value / (uint32_t) power(0x80, i);
		if (value > init_val) {
			dest[i] |= 0x80;
		}
		len++;
	}
	return len;
}

/**
 * Get variable integer value
 *
 * @param buf Byte array
 * @param pos how many bits rlen occupied
 * @return Integer value
 */
uint32_t
get_var_integer(const uint8_t *buf, uint32_t *pos)
{
	uint8_t  temp;
	uint32_t result = 0;

	uint32_t p = *pos;
	int      i = 0;

	do {
		temp   = *(buf + p);
		result = result + (uint32_t)(temp & 0x7f) * (power(0x80, i));
		p++;
	} while ((temp & 0x80) > 0 && i++ < 4);
	*pos = p;
	return result;
}

/**
 * Get utf-8 string
 *
 * @param dest output string
 * @param src input bytes
 * @param pos
 * @return string length -1: not utf-8, 0: empty string, >0 : normal utf-8
 * string
 */
int32_t
get_utf8_str(char **dest, const uint8_t *src, uint32_t *pos)
{
	int32_t str_len = 0;
	NNI_GET16(src + (*pos), str_len);

	*pos = (*pos) + 2;
	if (str_len > 0) {
		if (utf8_check((const char *) (src + *pos), str_len) ==
		    ERR_SUCCESS) {
			*dest = (char *) (src + (*pos));
			*pos  = (*pos) + str_len;
		} else {
			str_len = -1;
		}
	}
	return str_len;
}

/**
 * @brief safe copy limit size of src data to dest
 * 	  return null and -1 strlen if buffer overflow
 * @param src 
 * @param pos 
 * @param str_len target size of data
 * @param limit max size of data copied
 * @return uint8_t* NULL if overflow or not utf-8
 */
uint8_t *
copyn_utf8_str(const uint8_t *src, uint32_t *pos, int *str_len, int limit)
{
	*str_len      = 0;
	uint8_t *dest = NULL;

	NNI_GET16(src + (*pos), *str_len);

	*pos = (*pos) + 2;
	if (*str_len > (limit-2)) {
		//buffer overflow
		*str_len = -1;
		return NULL;
	}
	if (*str_len > 0) {
		if (utf8_check((const char *) (src + *pos), *str_len) ==
		    ERR_SUCCESS) {
			if ((dest = nng_alloc(*str_len + 1)) == NULL) {
				*str_len = 0;
				return NULL;
			}
			memcpy(dest, src + (*pos), *str_len);
			dest[*str_len] = '\0';
			*pos           = (*pos) + (*str_len);
		} else {
			*str_len = -1;
		}
	}
	return dest;
}

/**
 * copy utf-8 string to dst
 *
 * @param dest output string
 * @param src input bytes
 * @param pos
 * @return string length -1: not utf-8, 0: empty string, >0 : normal utf-8
 * string
 */
uint8_t *
copy_utf8_str(const uint8_t *src, uint32_t *pos, int *str_len)
{
	*str_len      = 0;
	uint8_t *dest = NULL;

	NNI_GET16(src + (*pos), *str_len);

	*pos = (*pos) + 2;
	if (*str_len > 0) {
		if (utf8_check((const char *) (src + *pos), *str_len) ==
		    ERR_SUCCESS) {
			if ((dest = nng_alloc(*str_len + 1)) == NULL) {
				*str_len = 0;
				return NULL;
			}
			memcpy(dest, src + (*pos), *str_len);
			dest[*str_len] = '\0';
			*pos           = (*pos) + (*str_len);
		} else {
			*str_len = -1;
		}
	}
	return dest;
}

/**
 * copy size of limit binary string to dst without utf8_check
 *
 * @param dest output string
 * @param src input bytes
 * @param pos
 * @return string length 0: empty string, >0 : normal utf-8
 * string
 */
uint8_t *
copyn_str(const uint8_t *src, uint32_t *pos, int *str_len, int limit)
{
	*str_len      = 0;
	uint8_t *dest = NULL;

	if (!src || !pos) {
		*str_len = 0;
		return NULL;
	}

	NNI_GET16(src + (*pos), *str_len);

	*pos = (*pos) + 2;
	if (*str_len > (limit-2)) {
		//buffer overflow
		*str_len = -1;
	}
	if (*str_len > 0) {
		if ((dest = nng_alloc(*str_len + 1)) == NULL || (src + (*pos) == NULL)) {
			*str_len = 0;
			return NULL;
		}
		memcpy(dest, src + (*pos), *str_len);
		dest[*str_len] = '\0';
		*pos           = (*pos) + (*str_len);
	}
	return dest;
}

int
utf8_check(const char *str, size_t len)
{
	int i;
	int j;
	int codelen;
	int codepoint;

	const unsigned char *ustr = (const unsigned char *) str;

	if (!str)
		return ERR_INVAL;
	if (len > 65536)
		return ERR_INVAL;

	for (i = 0; i < (int) len; i++) {
		if (ustr[i] == 0) {
			return ERR_MALFORMED_UTF8;
		} else if (ustr[i] <= 0x7f) {
			codelen   = 1;
			codepoint = ustr[i];
		} else if ((ustr[i] & 0xE0) == 0xC0) {
			/* 110xxxxx - 2 byte sequence */
			if (ustr[i] == 0xC0 || ustr[i] == 0xC1) {
				/* Invalid bytes */
				return ERR_MALFORMED_UTF8;
			}
			codelen   = 2;
			codepoint = (ustr[i] & 0x1F);
		} else if ((ustr[i] & 0xF0) == 0xE0) {
			/* 1110xxxx - 3 byte sequence */
			codelen   = 3;
			codepoint = (ustr[i] & 0x0F);
		} else if ((ustr[i] & 0xF8) == 0xF0) {
			/* 11110xxx - 4 byte sequence */
			if (ustr[i] > 0xF4) {
				/* Invalid, this would produce values >
				 * 0x10FFFF. */
				return ERR_MALFORMED_UTF8;
			}
			codelen   = 4;
			codepoint = (ustr[i] & 0x07);
		} else {
			/* Unexpected continuation byte. */
			return ERR_MALFORMED_UTF8;
		}

		/* Reconstruct full code point */
		if (i == (int) len - codelen + 1) {
			/* Not enough data */
			return ERR_MALFORMED_UTF8;
		}
		// check total len of packet in case overflow when there is
		// only one byte
		for (j = 0; j < codelen - 1 && len > 1; j++) {
			if ((ustr[++i] & 0xC0) != 0x80) {
				/* Not a continuation byte */
				return ERR_MALFORMED_UTF8;
			}
			codepoint = (codepoint << 6) | (ustr[i] & 0x3F);
		}

		/* Check for UTF-16 high/low surrogates */
		if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
			return ERR_MALFORMED_UTF8;
		}

		/* Check for overlong or out of range encodings */
		/* Checking codelen == 2 isn't necessary here, because it is
		 *already covered above in the C0 and C1 checks. if(codelen ==
		 *2 && codepoint < 0x0080){ return ERR_MALFORMED_UTF8; }else
		 */
		if (codelen == 3 && codepoint < 0x0800) {
			return ERR_MALFORMED_UTF8;
		} else if (codelen == 4 &&
		    (codepoint < 0x10000 || codepoint > 0x10FFFF)) {
			return ERR_MALFORMED_UTF8;
		}

		/* Check for non-characters */
		if (codepoint >= 0xFDD0 && codepoint <= 0xFDEF) {
			return ERR_MALFORMED_UTF8;
		}
		if ((codepoint & 0xFFFF) == 0xFFFE ||
		    (codepoint & 0xFFFF) == 0xFFFF) {
			return ERR_MALFORMED_UTF8;
		}
		/* Check for control characters */
		if (codepoint <= 0x001F ||
		    (codepoint >= 0x007F && codepoint <= 0x009F)) {
			return ERR_MALFORMED_UTF8;
		}
	}
	return ERR_SUCCESS;
}

uint16_t
get_variable_binary(uint8_t **dest, const uint8_t *src)
{
	uint16_t len = 0;
	NNI_GET16(src, len);
	*dest = (uint8_t *) (src + 2);
	return len;
}

// set header & remaining length of msg
int
fixed_header_adaptor(uint8_t *packet, nng_msg *dst)
{
	nni_msg *m;
	int      rv;
	uint32_t len;
	size_t   pos = 1;

	m   = (nni_msg *) dst;
	len = get_var_integer(packet, (uint32_t *) &pos);
	nni_msg_set_remaining_len(m, len);
	rv = nni_msg_header_append(m, packet, pos);
	return rv;
}
/**
 * @brief copy packet (original msg suppose have full MQTT bytes in payload) to
 * dst msg (new empty one)
 *
 * @param packet
 * @param dst assume it as an empty message
 * @return int
 */
int
ws_msg_adaptor(uint8_t *packet, nng_msg *dst)
{
	nni_msg *m;
	int      rv;
	uint32_t len;
	size_t   pos = 1;

	m   = (nni_msg *) dst;
	len = get_var_integer(packet, (uint32_t *) &pos);
	nni_msg_set_cmd_type(m, *packet & 0xf0);
	nni_msg_set_remaining_len(m, len);
	rv = nni_msg_header_append(m, packet, pos);

	if (len > 0) {
		nni_msg_append(m, packet + pos, len);
	}

	return rv;
}
/*
int variable_header_adaptor(uint8_t *packet, nni_msg *dst)
{
        nni_msg  *m;
        int      pos = 0;
        uint32_t len;
        return 0;
}
*/
/*
static char *client_id_gen(int *idlen, const char *auto_id_prefix, int
auto_id_prefix_len)
{
        char *client_id;
        return client_id;
}
}
*/

void
conn_param_set_property(conn_param *cparam, property *prop)
{
	property_data *prop_data =
	    property_get_value(prop, SESSION_EXPIRY_INTERVAL);
	if (prop_data) {
		cparam->session_expiry_interval = prop_data->p_value.u32;
	}
	prop_data = property_get_value(prop, RECEIVE_MAXIMUM);
	if (prop_data) {
		cparam->rx_max = prop_data->p_value.u16;
	}
	prop_data = property_get_value(prop, MAXIMUM_PACKET_SIZE);
	if (prop_data) {
		cparam->max_packet_size = prop_data->p_value.u32;
	}
	prop_data = property_get_value(prop, TOPIC_ALIAS_MAXIMUM);
	if (prop_data) {
		cparam->topic_alias_max = prop_data->p_value.u16;
	}
	prop_data = property_get_value(prop, REQUEST_RESPONSE_INFORMATION);
	if (prop_data) {
		cparam->req_resp_info = prop_data->p_value.u8;
	}
	prop_data = property_get_value(prop, REQUEST_PROBLEM_INFORMATION);
	if (prop_data) {
		cparam->req_problem_info = prop_data->p_value.u8;
	}

	prop_data = property_get_value(prop, AUTHENTICATION_METHOD);
	if (prop_data) {
		cparam->auth_method = &prop_data->p_value.str;
	}

	prop_data = property_get_value(prop, AUTHENTICATION_DATA);
	if (prop_data) {
		cparam->auth_method = &prop_data->p_value.binary;
	}

	prop_data = property_get_value(prop, USER_PROPERTY);
	if (prop_data) {
		cparam->user_property = &prop_data->p_value.strpair;
	}
}

void
conn_param_set_will_property(conn_param *cparam, property *prop)
{
	property_data *prop_data;
	prop_data = property_get_value(prop, WILL_DELAY_INTERVAL);
	if (prop_data) {
		// set expiried timestamp
		cparam->will_delay_interval = nng_clock() + prop_data->p_value.u32 * 1000;
	}
	prop_data = property_get_value(prop, PAYLOAD_FORMAT_INDICATOR);
	if (prop_data) {
		cparam->payload_format_indicator = prop_data->p_value.u8;
	}
	prop_data = property_get_value(prop, MESSAGE_EXPIRY_INTERVAL);
	if (prop_data) {
		cparam->msg_expiry_interval = prop_data->p_value.u32;
	}
	prop_data = property_get_value(prop, CONTENT_TYPE);
	if (prop_data) {
		cparam->content_type = &prop_data->p_value.str;
	}
	prop_data = property_get_value(prop, RESPONSE_TOPIC);
	if (prop_data) {
		cparam->resp_topic = &prop_data->p_value.str;
	}
	prop_data = property_get_value(prop, CORRELATION_DATA);
	if (prop_data) {
		cparam->corr_data = &prop_data->p_value.binary;
	}
	prop_data = property_get_value(prop, USER_PROPERTY);
	if (prop_data) {
		cparam->payload_user_property = &prop_data->p_value.strpair;
	}
}

/**
 * @brief handle and decode CONNECT packet
 * only use in nego_cb !!!
 * TODO CONNECT packet validation
 */
int32_t
conn_handler(uint8_t *packet, conn_param *cparam, size_t max)
{
	uint32_t len, tmp, pos = 0, len_of_var = 0;
	int      len_of_str = 0;
	int32_t  rv         = 0;

	if (packet[pos] != CMD_CONNECT) {
		return (-1);
	} else {
		pos++;
	}

	// remaining length
	len = (uint32_t) get_var_integer(packet + pos, &len_of_var);
	pos += len_of_var;
	// protocol name
	cparam->pro_name.body =
	    (char *) copyn_utf8_str(packet, &pos, &len_of_str, max-pos);
	cparam->pro_name.len = len_of_str;
	rv                   = len_of_str < 0 ? PROTOCOL_ERROR : 0;
	log_trace("pro_name: %s", cparam->pro_name.body);
	// protocol ver
	cparam->pro_ver = packet[pos];
	pos++;
	// connect flag
	cparam->con_flag    = packet[pos];
	cparam->clean_start = (cparam->con_flag & 0x02) >> 1;
	cparam->will_flag   = (cparam->con_flag & 0x04) >> 2;
	cparam->will_qos    = (cparam->con_flag & 0x18) >> 3;
	cparam->will_retain = (cparam->con_flag & 0x20) >> 5;
	log_trace("conn flag:%x", cparam->con_flag);
	pos++;
	// keepalive
	NNI_GET16(packet + pos, tmp);
	cparam->keepalive_mqtt = tmp;
	pos += 2;
	// properties
	if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
		log_trace("MQTT V5 Properties");
		cparam->properties = decode_buf_properties(
		    packet, len, &pos, &cparam->prop_len, true);
		if (cparam->properties) {
			conn_param_set_property(cparam, cparam->properties);
			if ((rv = check_properties(cparam->properties)) !=
			    SUCCESS) {
				return rv;
			}
		}
	}
	log_trace("pos after property: [%d]", pos);

	// payload client_id
	cparam->clientid.body =
	    (char *) copyn_utf8_str(packet, &pos, &len_of_str, max-pos);
	cparam->clientid.len = len_of_str;

	if (len_of_str == 0) {
		char clientid_r[20] = {0};
		snprintf(clientid_r, 20, "nanomq-%08x", nni_random());
		clientid_r[19]        = '\0';
		cparam->clientid.body = nng_strdup(clientid_r);
		cparam->clientid.len  = strlen(clientid_r);
		cparam->assignedid    = true;
	} else if (len_of_str < 0) {
		return (PROTOCOL_ERROR);
	}
	log_trace("clientid: [%s] [%d]", cparam->clientid.body, len_of_str);

	if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5 && cparam->assignedid) {
		property *assigned_cid =
		    property_set_value_str(ASSIGNED_CLIENT_IDENTIFIER,
		        cparam->clientid.body, cparam->clientid.len, false);
		if (cparam->properties == NULL) {
			cparam->properties = property_alloc();
		}
		property_append(cparam->properties, assigned_cid);
	}
	// will topic
	if (rv == 0 && cparam->will_flag != 0) {
		if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
			cparam->will_properties = decode_buf_properties(
			    packet, len, &pos, &cparam->will_prop_len, true);
			if (cparam->will_properties) {
				conn_param_set_will_property(
				    cparam, cparam->will_properties);
				if ((rv = check_properties(
				         cparam->will_properties)) !=
				    SUCCESS) {
					return rv;
				}
			}
		}
		cparam->will_topic.body =
		    (char *) copyn_utf8_str(packet, &pos, &len_of_str, max-pos);
		cparam->will_topic.len = len_of_str;
		rv                     = len_of_str < 0 ? 1 : 0;
		if (cparam->will_topic.body == NULL || rv != 0) {
			rv = PROTOCOL_ERROR;
			return rv;
		}
		log_trace("will_topic: %s %d", cparam->will_topic.body, rv);
		// will msg
		if (rv == 0) {
			if (cparam->payload_format_indicator == 0) {
				cparam->will_msg.body = (char *) copyn_str(
				    packet, &pos, &len_of_str, max - pos);
			} else if (rv == 0 &&
			    cparam->payload_format_indicator == 0x01) {
				cparam->will_msg.body =
				    (char *) copyn_utf8_str(
				        packet, &pos, &len_of_str, max - pos);
			}
			cparam->will_msg.len = len_of_str;
			rv = len_of_str < 0 ? PAYLOAD_FORMAT_INVALID : 0;
			log_trace(
			    "will_msg: %s %d", cparam->will_msg.body, rv);
		}
	}

	// username
	if (rv == 0 && (cparam->con_flag & 0x80) > 0) {
		cparam->username.body =
		    (char *) copyn_utf8_str(packet, &pos, &len_of_str, max-pos);
		cparam->username.len = len_of_str;
		rv                   = len_of_str < 0 ? PAYLOAD_FORMAT_INVALID : 0;
		if (rv != 0) {
			return rv;
		}
		log_trace(
		    "username: %s %d", cparam->username.body, len_of_str);
	}
	// password
	if (rv == 0 && (cparam->con_flag & 0x40) > 0) {
		cparam->password.body =
		    copyn_utf8_str(packet, &pos, &len_of_str, max-pos);
		cparam->password.len = len_of_str;
		rv                   = len_of_str < 0 ? PAYLOAD_FORMAT_INVALID : 0;
		if (rv != 0) {
			return rv;
		}
		log_trace(
		    "password: %s %d", cparam->password.body, len_of_str);
	}
	if (len + len_of_var + 1 != pos) {
		log_error("in connect handler");
		rv = PROTOCOL_ERROR;
	}
	return rv;
}

// /**
//  * @brief convert string from @format to utf-8 format
//  * caller is responsible to free the memory returned
//  */
// char *
// convert_to_utf8(char *src, char *format, size_t *len)
// {
// 	size_t  ascii_len     = 10, utf8_len;
// 	iconv_t iconv_obj     = iconv_open("utf-8", format);
// 	char   *out_str       = calloc(strlen(src) * 2, sizeof(char));
// 	char   *out_str_start = out_str;

// 	size_t in_str_bytes_left  = strlen(src);
// 	size_t out_str_bytes_left = strlen(src) * 2;
// 	int iconv_return = iconv(iconv_obj, &src, &in_str_bytes_left, &out_str,
// 	    &out_str_bytes_left);
// 	iconv_close(iconv_obj);
// 	*len = out_str_bytes_left;
// 	return out_str_start;
// }

/**
 * @brief handle and encode CONNACK packet
 */
void
nmq_connack_encode(nng_msg *msg, conn_param *cparam, uint8_t reason)
{
	uint8_t ack_flag = 0x00;
	nni_msg_append(msg, &ack_flag, 1);
	nni_msg_append(msg, &reason, 1);

	if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
		// TODO set properties if necessary
		encode_properties(msg, cparam->properties, CMD_CONNACK);
	}

	size_t         msg_len    = nng_msg_len(msg);
	uint8_t        var_len[4] = { 0 };
	struct pos_buf buf = { .curpos = &var_len[0], .endpos = &var_len[4] };

	int     bytes = write_variable_length_value(msg_len, &buf);
	uint8_t cmd   = CMD_CONNACK;
	nng_msg_header_append(msg, &cmd, 1);
	nng_msg_header_append(msg, var_len, bytes);
}

/**
 * @brief set session present byte alone
 * session true - session restored set to 1
 * session false - new start set to 0
 */
void
nmq_connack_session(nng_msg *msg, bool session)
{
	uint8_t *body = nni_msg_body(msg);
	if (session) {
		*body = 0x01;
	} else {
		*body = 0x00;
	}
}

static void
conn_param_init(conn_param *cparam)
{
	cparam->pro_name.len    = 0;
	cparam->pro_name.body   = NULL;
	cparam->clientid.len    = 0;
	cparam->clientid.body   = NULL;
	cparam->will_topic.body = NULL;
	cparam->will_topic.len  = 0;
	cparam->will_msg.body   = NULL;
	cparam->will_msg.len    = 0;
	cparam->username.body   = NULL;
	cparam->username.len    = 0;
	cparam->password.body   = NULL;
	cparam->password.len    = 0;
	cparam->assignedid      = false;

	// MQTT_v5 Variable header
	cparam->session_expiry_interval = 0;
	cparam->rx_max                  = 65535;
	cparam->max_packet_size         = 0;
	cparam->topic_alias_max         = 0;
	cparam->req_resp_info           = 0;
	cparam->req_problem_info        = 1;
	cparam->auth_method             = NULL;
	cparam->auth_data               = NULL;
	cparam->user_property           = NULL;

	// MQTT_v5 Will property ralation
	cparam->will_delay_interval      = 0;
	cparam->payload_format_indicator = 0;
	cparam->msg_expiry_interval      = 0;
	cparam->content_type             = NULL;
	cparam->resp_topic               = NULL;
	cparam->corr_data                = NULL;
	cparam->payload_user_property    = NULL;

	cparam->prop_len        = 0;
	cparam->properties      = NULL;
	cparam->will_prop_len   = 0;
	cparam->will_properties = NULL;
}

int
conn_param_alloc(conn_param **cparamp)
{
	conn_param *new_cp;
	if ((new_cp = nng_alloc(sizeof(conn_param))) == NULL) {
		return (NNG_ENOMEM);
	}
	nni_atomic_init(&new_cp->refcnt);
	nni_atomic_set(&new_cp->refcnt, 1);
	conn_param_init(new_cp);
	*cparamp = new_cp;
	return 0;
}

void
conn_param_free(conn_param *cparam)
{
	if (cparam == NULL) {
		return;
	}
	if (nni_atomic_dec_nv(&cparam->refcnt) != 0) {
		return;
	}
	log_trace("destroy conn param");
	nng_free(cparam->pro_name.body, cparam->pro_name.len);
	nng_free(cparam->clientid.body, cparam->clientid.len);
	nng_free(cparam->will_topic.body, cparam->will_topic.len);
	nng_free(cparam->will_msg.body, cparam->will_msg.len);
	nng_free(cparam->username.body, cparam->username.len);
	nng_free(cparam->password.body, cparam->password.len);

	property_free(cparam->properties);
	property_free(cparam->will_properties);


	nng_free(cparam, sizeof(struct conn_param));
	cparam = NULL;
}

void
conn_param_clone(conn_param *cparam)
{
	if (cparam == NULL) {
		return;
	}
	nni_atomic_inc(&cparam->refcnt);
}

uint32_t
DJBHash(char *str)
{
	unsigned int hash = 5381;
	while (*str) {
		hash = ((hash << 5) + hash) + (*str++); /* times 33 */
	}
	hash &= ~(1U << 31); /* strip the highest bit */
	return hash;
}

uint32_t
DJBHashn(char *str, uint16_t len)
{
	unsigned int hash = 5381;
	uint16_t     i    = 0;
	while (i < len) {
		hash = ((hash << 5) + hash) + (*str++); /* times 33 */
		i++;
	}
	hash &= ~(1U << 31); /* strip the highest bit */
	return hash;
}

/* Fowler/Noll/Vo (FNV) hash function, variant 1a */
uint32_t
fnv1a_hashn(char *str, size_t n)
{
    uint32_t hash = 0x811c9dc5;
    while (n--) {
        hash ^= (uint8_t) *str++;
        hash *= 0x01000193;
    }
    return hash;
}

uint8_t
crc_hashn(char *str, size_t n)
{
	uint8_t crc = 0xff;
	size_t  i, j;
	for (i = 0; i < n; i++) {
		crc ^= str[i];
		for (j = 0; j < 8; j++) {
			if ((crc & 0x80) != 0)
				crc = (uint8_t)((crc << 1) ^ 0x31);
			else
				crc <<= 1;
		}
	}
	return crc;
}

/* Refer. https://homes.cs.washington.edu/~suciu/XMLTK/xmill/www/XMILL/html/crc32_8c-source.html */
/* This crc table is created with polynomial (0xedb88320L) */
const uint32_t crc32_table[256] = {
  0x00000000L, 0x77073096L, 0xee0e612cL, 0x990951baL, 0x076dc419L,
  0x706af48fL, 0xe963a535L, 0x9e6495a3L, 0x0edb8832L, 0x79dcb8a4L,
  0xe0d5e91eL, 0x97d2d988L, 0x09b64c2bL, 0x7eb17cbdL, 0xe7b82d07L,
  0x90bf1d91L, 0x1db71064L, 0x6ab020f2L, 0xf3b97148L, 0x84be41deL,
  0x1adad47dL, 0x6ddde4ebL, 0xf4d4b551L, 0x83d385c7L, 0x136c9856L,
  0x646ba8c0L, 0xfd62f97aL, 0x8a65c9ecL, 0x14015c4fL, 0x63066cd9L,
  0xfa0f3d63L, 0x8d080df5L, 0x3b6e20c8L, 0x4c69105eL, 0xd56041e4L,
  0xa2677172L, 0x3c03e4d1L, 0x4b04d447L, 0xd20d85fdL, 0xa50ab56bL,
  0x35b5a8faL, 0x42b2986cL, 0xdbbbc9d6L, 0xacbcf940L, 0x32d86ce3L,
  0x45df5c75L, 0xdcd60dcfL, 0xabd13d59L, 0x26d930acL, 0x51de003aL,
  0xc8d75180L, 0xbfd06116L, 0x21b4f4b5L, 0x56b3c423L, 0xcfba9599L,
  0xb8bda50fL, 0x2802b89eL, 0x5f058808L, 0xc60cd9b2L, 0xb10be924L,
  0x2f6f7c87L, 0x58684c11L, 0xc1611dabL, 0xb6662d3dL, 0x76dc4190L,
  0x01db7106L, 0x98d220bcL, 0xefd5102aL, 0x71b18589L, 0x06b6b51fL,
  0x9fbfe4a5L, 0xe8b8d433L, 0x7807c9a2L, 0x0f00f934L, 0x9609a88eL,
  0xe10e9818L, 0x7f6a0dbbL, 0x086d3d2dL, 0x91646c97L, 0xe6635c01L,
  0x6b6b51f4L, 0x1c6c6162L, 0x856530d8L, 0xf262004eL, 0x6c0695edL,
  0x1b01a57bL, 0x8208f4c1L, 0xf50fc457L, 0x65b0d9c6L, 0x12b7e950L,
  0x8bbeb8eaL, 0xfcb9887cL, 0x62dd1ddfL, 0x15da2d49L, 0x8cd37cf3L,
  0xfbd44c65L, 0x4db26158L, 0x3ab551ceL, 0xa3bc0074L, 0xd4bb30e2L,
  0x4adfa541L, 0x3dd895d7L, 0xa4d1c46dL, 0xd3d6f4fbL, 0x4369e96aL,
  0x346ed9fcL, 0xad678846L, 0xda60b8d0L, 0x44042d73L, 0x33031de5L,
  0xaa0a4c5fL, 0xdd0d7cc9L, 0x5005713cL, 0x270241aaL, 0xbe0b1010L,
  0xc90c2086L, 0x5768b525L, 0x206f85b3L, 0xb966d409L, 0xce61e49fL,
  0x5edef90eL, 0x29d9c998L, 0xb0d09822L, 0xc7d7a8b4L, 0x59b33d17L,
  0x2eb40d81L, 0xb7bd5c3bL, 0xc0ba6cadL, 0xedb88320L, 0x9abfb3b6L,
  0x03b6e20cL, 0x74b1d29aL, 0xead54739L, 0x9dd277afL, 0x04db2615L,
  0x73dc1683L, 0xe3630b12L, 0x94643b84L, 0x0d6d6a3eL, 0x7a6a5aa8L,
  0xe40ecf0bL, 0x9309ff9dL, 0x0a00ae27L, 0x7d079eb1L, 0xf00f9344L,
  0x8708a3d2L, 0x1e01f268L, 0x6906c2feL, 0xf762575dL, 0x806567cbL,
  0x196c3671L, 0x6e6b06e7L, 0xfed41b76L, 0x89d32be0L, 0x10da7a5aL,
  0x67dd4accL, 0xf9b9df6fL, 0x8ebeeff9L, 0x17b7be43L, 0x60b08ed5L,
  0xd6d6a3e8L, 0xa1d1937eL, 0x38d8c2c4L, 0x4fdff252L, 0xd1bb67f1L,
  0xa6bc5767L, 0x3fb506ddL, 0x48b2364bL, 0xd80d2bdaL, 0xaf0a1b4cL,
  0x36034af6L, 0x41047a60L, 0xdf60efc3L, 0xa867df55L, 0x316e8eefL,
  0x4669be79L, 0xcb61b38cL, 0xbc66831aL, 0x256fd2a0L, 0x5268e236L,
  0xcc0c7795L, 0xbb0b4703L, 0x220216b9L, 0x5505262fL, 0xc5ba3bbeL,
  0xb2bd0b28L, 0x2bb45a92L, 0x5cb36a04L, 0xc2d7ffa7L, 0xb5d0cf31L,
  0x2cd99e8bL, 0x5bdeae1dL, 0x9b64c2b0L, 0xec63f226L, 0x756aa39cL,
  0x026d930aL, 0x9c0906a9L, 0xeb0e363fL, 0x72076785L, 0x05005713L,
  0x95bf4a82L, 0xe2b87a14L, 0x7bb12baeL, 0x0cb61b38L, 0x92d28e9bL,
  0xe5d5be0dL, 0x7cdcefb7L, 0x0bdbdf21L, 0x86d3d2d4L, 0xf1d4e242L,
  0x68ddb3f8L, 0x1fda836eL, 0x81be16cdL, 0xf6b9265bL, 0x6fb077e1L,
  0x18b74777L, 0x88085ae6L, 0xff0f6a70L, 0x66063bcaL, 0x11010b5cL,
  0x8f659effL, 0xf862ae69L, 0x616bffd3L, 0x166ccf45L, 0xa00ae278L,
  0xd70dd2eeL, 0x4e048354L, 0x3903b3c2L, 0xa7672661L, 0xd06016f7L,
  0x4969474dL, 0x3e6e77dbL, 0xaed16a4aL, 0xd9d65adcL, 0x40df0b66L,
  0x37d83bf0L, 0xa9bcae53L, 0xdebb9ec5L, 0x47b2cf7fL, 0x30b5ffe9L,
  0xbdbdf21cL, 0xcabac28aL, 0x53b39330L, 0x24b4a3a6L, 0xbad03605L,
  0xcdd70693L, 0x54de5729L, 0x23d967bfL, 0xb3667a2eL, 0xc4614ab8L,
  0x5d681b02L, 0x2a6f2b94L, 0xb40bbe37L, 0xc30c8ea1L, 0x5a05df1bL,
  0x2d02ef8dL
};

uint32_t
crc32_hashn(char *str, size_t n)
{
	uint32_t crc = 0xffffffff;
	while (n >= 8) {
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		n -= 8;
	}
	if (n)
		do {
			crc = crc32_table[((int)crc ^ (*str++)) & 0xff] ^ (crc >> 8);
		} while (--n);
	return crc ^ 0xffffffff;
}

/* CRC32C from https://github.com/confluentinc/librdkafka/blob/master/src/crc32c.c */
#define POLYCRC32C 0x82f63b78
static uint32_t crc32c_table[8][256];
static int      crc32c_init = 0;

/* Construct table for software CRC-32C calculation. */
static void crc32c_init_sw(void)
{
    uint32_t n, crc, k;

    for (n = 0; n < 256; n++) {
        crc = n;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc = crc & 1 ? (crc >> 1) ^ POLYCRC32C : crc >> 1;
        crc32c_table[0][n] = crc;
    }
    for (n = 0; n < 256; n++) {
        crc = crc32c_table[0][n];
        for (k = 1; k < 8; k++) {
            crc = crc32c_table[0][crc & 0xff] ^ (crc >> 8);
            crc32c_table[k][n] = crc;
        }
    }
}

/* Table-driven software version as a fall-back.  This is about 15 times slower
   than using the hardware instructions.  This assumes little-endian integers,
   as is the case on Intel processors that the assembler code here is for. */
static uint32_t crc32c_sw(uint32_t crci, const void *buf, size_t len)
{
    const unsigned char *next = buf;
    uint64_t crc;

    crc = crci ^ 0xffffffff;
    while (len && ((uintptr_t)next & 7) != 0) {
        crc = crc32c_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    while (len >= 8) {
        /* Alignment-safe */
        uint64_t ncopy;
        memcpy(&ncopy, next, sizeof(ncopy));
        crc ^= le64toh(ncopy);
        crc = crc32c_table[7][crc & 0xff] ^
              crc32c_table[6][(crc >> 8) & 0xff] ^
              crc32c_table[5][(crc >> 16) & 0xff] ^
              crc32c_table[4][(crc >> 24) & 0xff] ^
              crc32c_table[3][(crc >> 32) & 0xff] ^
              crc32c_table[2][(crc >> 40) & 0xff] ^
              crc32c_table[1][(crc >> 48) & 0xff] ^
              crc32c_table[0][crc >> 56];
        next += 8;
        len -= 8;
    }
    while (len) {
        crc = crc32c_table[0][(crc ^ *next++) & 0xff] ^ (crc >> 8);
        len--;
    }
    return (uint32_t)crc ^ 0xffffffff;
}

uint32_t
crc32c_hashn(char *str, size_t n)
{
	if (crc32c_init == 0) {
		crc32c_init_sw();
		crc32c_init = 1;
	}

	return crc32c_sw(0, (void *)str, n);
}

uint64_t
nano_hash(char *str)
{
	uint64_t hash = 5381;
	int      c;

	while ((c = *str++))
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
	                                         // hash = hash * 33 + c;
	return hash;
}

inline void
nano_msg_set_dup(nng_msg *msg)
{
	uint8_t *header;

	header  = nni_msg_header(msg);
	*header = *header | 0x08;
}
/**
 * @brief compose a MQTT V5 DISCONNECT msg from server side
 *        ref & rstr is not effective yet
 * 
 * @param msgp msg pointer
 * @param code reason code
 * @param rstr reason string
 * @param ref  server reference
 * @param prop user property
 * @return nng_msg*
 */
nng_msg *
nano_dismsg_composer(reason_code code, char* rstr, uint8_t *ref, property *prop)
{
	NNI_ARG_UNUSED(rstr);
	NNI_ARG_UNUSED(ref);
	NNI_ARG_UNUSED(prop);
	uint8_t  buf[5] = { 0x00 };
	nni_msg *msg;

	nni_msg_alloc(&msg, 0);

	switch (code)
	{
	case PROTOCOL_ERROR:
		buf[0] = (uint8_t)PROTOCOL_ERROR;
		nng_msg_append(msg, buf, 1);
		break;
	default:
		buf[0] = (uint8_t)UNSPECIFIED_ERROR;
		nng_msg_append(msg, buf, 1);
		break;
	}

	nng_msg_append(msg, buf+1, 1);
	buf[0] = CMD_DISCONNECT;
	buf[1] = 2;
	nng_msg_header_append(msg, buf, 2);
	nng_msg_set_cmd_type(msg, CMD_DISCONNECT);
	return msg;
}

// alloc a publish msg according to the need
nng_msg *
nano_pubmsg_composer(nng_msg **msgp, uint8_t retain, uint8_t qos,
    mqtt_string *payload, mqtt_string *topic, uint8_t proto_ver, nng_time time)
{
	size_t   rlen;
	uint8_t *ptr, buf[5] = { '\0' };
	uint32_t len;
	nni_msg *msg;

	len = payload->len + topic->len + 2;
	len += proto_ver == MQTT_PROTOCOL_VERSION_v5 ? 1 : 0;

	msg = *msgp;
	if (msg == NULL) {
		nni_msg_alloc(&msg, len + (qos > 0 ? 2 : 0));
	} else {
		nni_msg_realloc(msg, len + (qos > 0 ? 2 : 0));
	}

	nni_msg_set_timestamp(msg, time);
	if (qos > 0) {
		rlen = put_var_integer(buf + 1, len + 2);
		nni_msg_set_remaining_len(msg, len + 2);
		if (qos == 1) {
			buf[0] = CMD_PUBLISH | 0x02;
		} else if (qos == 2) {
			buf[0] = CMD_PUBLISH | 0x04;
		} else {
			nni_println("ERROR: will msg qos invalid");
			return NULL;
		}
	} else {
		rlen = put_var_integer(buf + 1, len);
		nni_msg_set_remaining_len(msg, len);
		buf[0] = CMD_PUBLISH;
	}
	ptr = nni_msg_header(msg);
	if (retain > 0) {
		buf[0] = buf[0] | 0x01;
	}
	memcpy(ptr, buf, rlen + 1);

	ptr = nni_msg_body(msg);
	NNI_PUT16(ptr, topic->len);
	ptr = ptr + 2;
	memcpy(ptr, topic->body, topic->len);
	ptr += topic->len;
	if (qos > 0) {
		// Set pid?
		NNI_PUT16(ptr, 0x10);
		ptr = ptr + 2;
	}

	if (proto_ver == MQTT_PROTOCOL_VERSION_v5) {
		uint8_t property_len = 0;
		memcpy(ptr, &property_len, 1);
		++ptr;
	}

	memcpy(ptr, payload->body, payload->len);
	nni_msg_set_payload_ptr(msg, ptr);

	return msg;
}

uint8_t
verify_connect(conn_param *cparam, conf *conf)
{
	int   i, n = conf->auths.count;
	char *username = (char *) cparam->username.body;
	char *password = (char *) cparam->password.body;

	if ((!conf->auths.enable) || conf->auths.count == 0 ||
	    conf->allow_anonymous == true) {
		log_trace("no valid entry in auth list");
		return 0;
	}

	if (cparam->username.len == 0 || cparam->password.len == 0) {
		if (cparam->pro_ver == 5) {
			return BAD_USER_NAME_OR_PASSWORD;
		} else {
			return 0x04;
		}
	}

	for (i = 0; i < n; i++) {
		if (strcmp(username, conf->auths.usernames[i]) == 0 &&
		    strcmp(password, conf->auths.passwords[i]) == 0) {
			return 0;
		}
	}
	if (cparam->pro_ver == 5) {
		return BAD_USER_NAME_OR_PASSWORD;
	} else {
		return 0x05;
	}
}

nng_msg *
nano_msg_notify_disconnect(conn_param *cparam, uint8_t code)
{
	nni_msg *   msg = NULL;
	mqtt_string string, topic;
	char        buff[256];
	snprintf(buff, 256, DISCONNECT_MSG, (char *) cparam->username.body,
	    nni_timestamp(), code, (char *) cparam->clientid.body);
	string.body = buff;
	string.len  = strlen(string.body);
	topic.body  = DISCONNECT_TOPIC;
	topic.len   = strlen(DISCONNECT_TOPIC);
	// V4 notification msg as default
	msg = nano_pubmsg_composer(
	    &msg, 0, 0, &string, &topic, cparam->pro_ver, nng_clock());
	return msg;
}

nng_msg *
nano_msg_notify_connect(conn_param *cparam, uint8_t code)
{
	nni_msg *   msg = NULL;
	mqtt_string string, topic;
	char        buff[256];

	snprintf(buff, 256, CONNECT_MSG, cparam->username.body,
	    nni_timestamp(), cparam->pro_name.body, cparam->keepalive_mqtt,
	    code, cparam->pro_ver, cparam->clientid.body, cparam->clean_start);
	string.body = buff;
	string.len  = strlen(string.body);
	topic.body  = CONNECT_TOPIC;
	topic.len   = strlen(CONNECT_TOPIC);
	msg         = nano_pubmsg_composer(
            &msg, 0, 0, &string, &topic, cparam->pro_ver, nng_clock());
	return msg;
}

/**
 * @brief
 *
 * @param msg SUB/UNSUB packet
 * @param root root node of nano_pipe_db linked table
 * @param cparam connection param
 * @return nano_pipe_db* pointer of newly added pipe_db
 */
nano_pipe_db *
nano_msg_get_subtopic(nni_msg *msg, nano_pipe_db *root, conn_param *cparam)
{
	char *        topic;
	nano_pipe_db *db = NULL, *tmp = NULL, *iter = NULL;
	uint8_t       len_of_topic = 0, *payload_ptr;
	uint32_t      len, len_of_varint = 0;
	size_t        bpos = 0, remain = 0;
	bool          repeat = false;

	if (nni_msg_get_type(msg) != CMD_SUBSCRIBE)
		return NULL;

	if (root != NULL) {
		db = root;
		while (db->next != NULL) {
			db = db->next;
		}
	}

	if (cparam->pro_ver == MQTT_PROTOCOL_VERSION_v5) {
		len = get_var_integer(
		    (uint8_t *) nni_msg_body(msg) + 2, &len_of_varint);
		payload_ptr =
		    (uint8_t *) nni_msg_body(msg) + 2 + len + len_of_varint;
	} else {
		payload_ptr = (uint8_t *) nni_msg_body(msg) + 2;
	}
	nni_msg_set_payload_ptr(msg, payload_ptr);
	remain = nni_msg_remaining_len(msg) - 2;

	while (bpos < remain) {
		NNI_GET16(payload_ptr + bpos, len_of_topic);

		if (len_of_topic != 0) {

			log_trace("The current process topic is %s",
			    payload_ptr + bpos + 2);
			iter = root;
			while (iter) {
				if (strlen(iter->topic) == len_of_topic &&
				    !strncmp((char *) (payload_ptr + bpos + 2),
				        iter->topic, len_of_topic)) {
					repeat = true;
					bpos += (2 + len_of_topic);
					if (iter->qos !=
					    *(payload_ptr + bpos)) {
						iter->qos =
						    *(payload_ptr + bpos);
					}
					bpos += 1;
				}
				iter = iter->next;
			}

			if (repeat) {
				repeat = false;
				continue;
			}

			if (NULL != db) {
				tmp = db;
				db  = db->next;
			}
			db       = nng_alloc(sizeof(nano_pipe_db));
			topic    = nng_alloc(len_of_topic + 1);
			db->prev = tmp;
			if (bpos == 0 && root == NULL) {
				root = db;
			} else {
				tmp->next = db;
			}
			db->root = root;
			if (topic == NULL || db == NULL) {
				NNI_ASSERT("ERROR: nng_alloc");
				return NULL;
			} else {
				bpos += 2;
			}
			strncpy(
			    topic, (char *) payload_ptr + bpos, len_of_topic);
			topic[len_of_topic] = 0x00;
			db->topic           = topic;
			bpos += len_of_topic;
		} else {
			NNI_ASSERT("ERROR : topic length error.");
			return NULL;
		}
		db->qos  = *(payload_ptr + bpos);
		db->next = NULL;
		log_trace("sub topic: %s qos : %x\n", db->topic, db->qos);
		bpos += 1;
	}

	return root;
}

void
nano_msg_free_pipedb(nano_pipe_db *db)
{
	uint8_t       len;
	nano_pipe_db *db_next;

	if (NULL == db) {
		return;
	}
	db = db->root;

	while (db) {
		len = strlen(db->topic);
		nng_free(db->topic, len);
		db_next = db->next;
		nng_free(db, sizeof(nano_pipe_db));
		db = db_next;
	}
	return;
}

void
nano_msg_ubsub_free(nano_pipe_db *db)
{
	nano_pipe_db *ptr, *tmp;
	uint8_t       len;

	if (NULL == db) {
		return;
	}
	if (db == db->root) {
		ptr = db;
		tmp = db->next;
		while (ptr) {
			ptr->root = tmp;
			ptr       = ptr->next;
		}
	} else {
		tmp            = db->prev;
		tmp->next      = db->next;
		db->next->prev = tmp;
	}

	len = strlen(db->topic);
	nng_free(db->topic, len);
	nng_free(db, sizeof(nano_pipe_db));
	return;
}

static int
nmq_subinfol_add_or(nni_list *l, struct subinfo *n)
{
	struct subinfo *sn = NULL;
	NNI_LIST_FOREACH(l, sn) {
		if (0 == strcmp(n->topic, sn->topic)) {
			return -1;
		}
	}
	nni_list_append(l, n);
	return 0;
}

static void *
nmq_subinfol_rm_or(nni_list *l, struct subinfo *n)
{
	struct subinfo *sn = NULL;
	NNI_LIST_FOREACH(l, sn) {
		if (0 == strcmp(n->topic, sn->topic)) {
			break;
		}
	}
	if (sn) {
		nni_list_remove(l, sn);
		return sn;
	}
	return NULL;
}

/**
 * @brief decode sub for subid, topics and RAP to subinfol
 * 	  warning only use with sub msg & V5 client
 *
 * @param msg
 * @param ptr to subinfol
 * @return int -1: protocol error; -2: unknown error; num:numbers of topics
 */
int
nmq_subinfo_decode(nng_msg *msg, void *l, uint8_t ver)
{
	char           *topic;
	uint8_t         *payload_ptr, *var_ptr;
	uint32_t        num = 0, len, len_of_varint = 0, len_of_str = 0, subid = 0;
	uint16_t        len_of_topic = 0;
	size_t          bpos = 0, remain = 0;
	struct subinfo *sn = NULL;
	nni_list       *ll = l;

	if (!l || !msg)
		return (-1);

	var_ptr = nni_msg_body(msg);
	len = 0;
	len_of_varint = 0;
	if (ver == MQTT_PROTOCOL_VERSION_v5)
		len = get_var_integer(
		    (uint8_t *) nni_msg_body(msg) + 2, &len_of_varint);
	payload_ptr = (uint8_t *) nni_msg_body(msg) + 2 + len + len_of_varint;

	int pos = 2 + len_of_varint, target_pos = 2 + len_of_varint + len;
	while (pos < target_pos) {
		switch (*(var_ptr + pos)) {
		case USER_PROPERTY:
			// key
			NNI_GET16(var_ptr + pos, len_of_str);
			pos += len_of_str;
			len_of_str = 0;
			// value
			NNI_GET16(var_ptr + pos, len_of_str);
			pos += len_of_str;
			len_of_str = 0;
			break;
		case SUBSCRIPTION_IDENTIFIER:
			subid = get_var_integer(var_ptr + pos, &len_of_varint);
			if (subid == 0)
				return (-1);
			pos += len_of_varint;
			break;
		default:
			log_error("Invalid property id");
			return (-2);
		}
	}
	if (pos > target_pos)
		return (-2);

	remain = nni_msg_remaining_len(msg) - target_pos;

	while (bpos < remain) {
		// Check the index of topic len
		if (bpos + 2 > remain)
			return (-3);
		NNI_GET16(payload_ptr + bpos, len_of_topic);

		if (len_of_topic == 0)
			continue;
		bpos += 2;
		// Check the index of topic body
		if (bpos + len_of_topic > remain)
			return (-3);

		log_trace(
		    "The current process topic is %s", payload_ptr + bpos);
		if ((sn = nng_alloc(sizeof(struct subinfo))) == NULL)
			return (-2);
		if ((topic = nng_alloc(len_of_topic + 1)) == NULL)
			return (-2);

		strncpy(topic, (char *) payload_ptr + bpos, len_of_topic);
		topic[len_of_topic] = 0x00;

		sn->topic = topic;
		bpos += len_of_topic;
		// Check the index of topic option
		if (bpos > remain)
			return (-3);

		sn->subid = subid;
		// qos no_local rap retain_handling
		sn->qos      = (uint8_t) ((0x03 & *(payload_ptr + bpos)));
		sn->no_local = (uint8_t) ((0x04 & *(payload_ptr + bpos)));
		sn->rap      = (uint8_t) ((0x08 & *(payload_ptr + bpos)) > 0);
		sn->retain_handling = (uint8_t) ((0x1f & *(payload_ptr + bpos)));
		memcpy(sn, payload_ptr + bpos, 1);
		NNI_LIST_NODE_INIT(&sn->node);

		if (0 != nmq_subinfol_add_or(ll, sn)) {
			// already exists
			nng_free(sn->topic, strlen(sn->topic));
			nng_free(sn, sizeof(*sn));
		}

		bpos += 1;
		num++;
	}

	return num;
}

/**
 * @brief decode unsub and remove subid, topics and RAP from subinfol
 * 	  warning only use with unsub msg & V5 client
 *
 * @param msg
 * @param ptr to subinfol
 * @return int -1: protocol error; -2: unknown error; num:numbers of topics
 */
int
nmq_unsubinfo_decode(nng_msg *msg, void *l, uint8_t ver)
{
	char           *topic;
	uint8_t         len_of_topic = 0, *payload_ptr, *var_ptr;
	uint32_t        num = 0, len, len_of_varint = 0, len_of_str = 0;
	size_t          bpos = 0, remain = 0;
	struct subinfo *sn = NULL, *sn2, snode;
	nni_list       *ll = l;

	if (!l || !msg)
		return (-1);

	len = 0;
	len_of_varint = 0;
	if (ver == MQTT_PROTOCOL_VERSION_v5)
		len = get_var_integer(
		    (uint8_t *) nni_msg_body(msg) + 2, &len_of_varint);

	var_ptr     = (uint8_t *) nni_msg_body(msg);
	payload_ptr = (uint8_t *) nni_msg_body(msg) + 2 + len + len_of_varint;
	int pos = 2 + len_of_varint, target_pos = 2 + len_of_varint + len;

	while (pos < target_pos) {
		switch (*(var_ptr + pos)) {
		case USER_PROPERTY:
			// key
			NNI_GET16(var_ptr + pos, len_of_str);
			pos += len_of_str;
			len_of_str = 0;
			// value
			NNI_GET16(var_ptr + pos, len_of_str);
			pos += len_of_str;
			len_of_str = 0;
			break;
		default:
			log_error("Invalid property id");
			return (-2);
		}
	}
	if (pos > target_pos)
		return (-2);

	remain = nni_msg_remaining_len(msg) - target_pos;

	while (bpos < remain) {
		// Check the index of topic len
		if (bpos + 2 > remain)
			return (-3);
		NNI_GET16(payload_ptr + bpos, len_of_topic);

		if (len_of_topic == 0)
			continue;
		bpos += 2;
		// Check the index of topic body
		if (bpos + len_of_topic > remain)
			return (-3);

		log_trace(
		    "The current process topic is %s", payload_ptr + bpos);
		if ((topic = nng_alloc(len_of_topic + 1)) == NULL)
			return (-2);

		strncpy(topic, (char *) payload_ptr + bpos, len_of_topic);
		topic[len_of_topic] = 0x00;

		bpos += len_of_topic;
		// Check the index of topic option
		if (bpos > remain)
			return (-3);

		snode.topic = topic;
		sn = &snode;
		if (NULL != (sn2 = nmq_subinfol_rm_or(ll, sn))) {
			log_trace("Topic %s free from subinfol", sn2->topic);
			nng_free(sn2->topic, strlen(sn2->topic));
			nng_free(sn2, sizeof(*sn2));
		}
		nng_free(topic, len_of_topic+1);

		num++;
	}

	return num;
}


static int
topic_count(const char *topic)
{
	int         cnt = 0;
	const char *t   = topic;

	while (t) {
		// log_info("%s", t);
		t = strchr(t, '/');
		cnt++;
		if (t == NULL) {
			break;
		}
		t++;
	}

	return cnt;
}

static void
topic_queue_free(char **topic_queue)
{
	char * t  = NULL;
	char **tq = topic_queue;

	while (*topic_queue) {
		t = *topic_queue;
		topic_queue++;
		zfree(t);
		t = NULL;
	}

	if (tq) {
		zfree(tq);
	}
}

static char **
topic_parse(const char *topic)
{
	if (topic == NULL) {
		// log_err("topic is NULL");
		return NULL;
	}

	int         row   = 0;
	int         len   = 2;
	const char *b_pos = topic;
	char *      pos   = NULL;

	int cnt = topic_count(topic);

	// Here we will get (cnt + 1) memory, one for NULL end
	char **topic_queue = (char **) zmalloc(sizeof(char *) * (cnt + 1));

	while ((pos = strchr(b_pos, '/')) != NULL) {

		len              = pos - b_pos + 1;
		topic_queue[row] = (char *) zmalloc(sizeof(char) * len);
		memcpy(topic_queue[row], b_pos, (len - 1));
		topic_queue[row][len - 1] = '\0';
		b_pos                     = pos + 1;
		row++;
	}

	len = strlen(b_pos);

	topic_queue[row] = (char *) zmalloc(sizeof(char) * (len + 1));
	memcpy(topic_queue[row], b_pos, (len));
	topic_queue[row][len] = '\0';
	topic_queue[++row]    = NULL;

	return topic_queue;
}


bool
check_ifwildcard(const char *w, const char *n)
{
	char **w_q    = topic_parse(w);
	char **n_q    = topic_parse(n);
	char **wq_free = w_q;
	char **nq_free = n_q;
	bool   result = true;
	bool   flag   = false;

	while (*w_q != NULL && *n_q != NULL) {
		// printf("w: %s, n: %s\n", *w_q, *n_q);
		if (strcmp(*w_q, *n_q) != 0) {
			if (strcmp(*w_q, "#") == 0) {
				flag = true;
				break;
			} else if (strcmp(*w_q, "+") != 0) {
				result = false;
				break;
			}
		}
		w_q++;
		n_q++;
	}

	if (*w_q && strcmp(*w_q, "#") == 0) {
		flag = true;
	}
	if (*w_q && strcmp(*w_q, "+") == 0) {
		flag = false;
	}

	if (!flag) {
		if (*w_q || *n_q) {
			result = false;
		}
	}

	topic_queue_free(wq_free);
	topic_queue_free(nq_free);

	// printf("value: %d\n", result);
	return result;
}

/**
 * @brief check if there is any topic matched with
 * 		  *origin
 * 
 * @param origin topic with wildcard 
 * @param input  topic in pub packet
 * @return true 
 * @return false 
 */
bool
topic_filter(const char *origin, const char *input)
{
	if (strcmp(origin, input) == 0) {
		return true;
	}
	return check_ifwildcard(origin, input);
}

bool
topic_filtern(const char *origin, const char *input, size_t n)
{
	char *buff = nni_zalloc(n + 1);
	strncpy(buff, input, n);
	bool res = false;
	size_t len = strlen(origin);
	if (len == n && strncmp(origin, input, n) == 0) {
		res = true;
	} else {
		res = check_ifwildcard(origin, buff);
	}
	nni_free(buff, n + 1);
	return res;
}
