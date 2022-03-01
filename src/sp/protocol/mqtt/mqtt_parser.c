//
// Copyright 2020 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/protocol/mqtt/mqtt_parser.h"
#include "core/nng_impl.h"
#include "nng/nng_debug.h"
#include "nng/protocol/mqtt/mqtt.h"

#include "nng/mqtt/packet.h"
#include <conf.h>
// #include <iconv.h>
#include <stdio.h>
#include <string.h>

struct pub_extra {
	uint8_t  qos;
	uint16_t packet_id;
	void    *msg;
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

/**
 * byte array to hex string
 *
 * @param src
 * @param dest
 * @param src_len
 * @return
 */
static char *
bytes_to_str(const unsigned char *src, char *dest, int src_len)
{
	int  i;
	char szTmp[4] = { 0 };

	for (i = 0; i < src_len; i++) {
		sprintf(szTmp, "%02X ", src[i]);
		memcpy(dest + (i * 3), szTmp, 3);
	}
	return dest;
}

static void
print_hex(const char *prefix, const unsigned char *src, int src_len)
{
	if (src_len > 0) {
		char *dest = (char *) nng_zalloc(src_len * 3 + 1);

		if (dest == NULL) {
			debug_msg("alloc fail!");
			return;
		}
		dest = bytes_to_str(src, dest, src_len);

		debug_msg("%s%s", prefix, dest);

		nng_free(dest, src_len * 3 + 1);
	}
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
 * @param pos
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
		result = result + (uint32_t) (temp & 0x7f) * (power(0x80, i));
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
		if ((dest = nng_alloc(*str_len + 1)) == NULL) {
			*str_len = 0;
			return NULL;
		}
		if (utf8_check((const char *) (src + *pos), *str_len) ==
		    ERR_SUCCESS) {
			memcpy(dest, src + (*pos), *str_len);
			dest[*str_len] = '\0';
			*pos           = (*pos) + (*str_len);
		} else {
			nng_free(dest, *str_len + 1);
			dest     = NULL;
			*str_len = -1;
		}
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
		for (j = 0; j < codelen - 1; j++) {
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
ws_fixed_header_adaptor(uint8_t *packet, nng_msg *dst)
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

/**
 * @brief handle and decode CONNECT packet
 * only use in nego_cb !!!
 * TODO CONNECT packet validation
 */
int32_t
conn_handler(uint8_t *packet, conn_param *cparam)
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
	    (char *) copy_utf8_str(packet, &pos, &len_of_str);
	cparam->pro_name.len = len_of_str;
	rv                   = len_of_str < 0 ? 1 : 0;
	debug_msg("pro_name: %s", cparam->pro_name.body);
	// protocol ver
	cparam->pro_ver = packet[pos];
	pos++;
	// connect flag
	cparam->con_flag    = packet[pos];
	cparam->clean_start = (cparam->con_flag & 0x02) >> 1;
	cparam->will_flag   = (cparam->con_flag & 0x04) >> 2;
	cparam->will_qos    = (cparam->con_flag & 0x18) >> 3;
	cparam->will_retain = (cparam->con_flag & 0x20) >> 5;
	debug_msg("conn flag:%x", cparam->con_flag);
	pos++;
	// keepalive
	NNI_GET16(packet + pos, tmp);
	cparam->keepalive_mqtt = tmp;
	pos += 2;
	// properties
	if (cparam->pro_ver == PROTOCOL_VERSION_v5) {
		debug_msg("MQTT 5 Properties");
		cparam->properties = decode_buf_properties(
		    packet, len, &pos, &cparam->prop_len, false);
	}
	debug_msg("pos after property: [%d]", pos);

	// payload client_id
	cparam->clientid.body =
	    (char *) copy_utf8_str(packet, &pos, &len_of_str);
	cparam->clientid.len = len_of_str;

	if (len_of_str == 0) {
		char *clientid_r = nng_alloc(20);
		snprintf(clientid_r, 20, "nanomq-%08x", nni_random());
		clientid_r[19]        = '\0';
		cparam->clientid.body = clientid_r;
		cparam->clientid.len  = 20;
		cparam->assignedid    = true;
	} else if (len_of_str < 0) {
		return (1);
	}
	debug_msg("clientid: [%s] [%d]", cparam->clientid.body, len_of_str);

	// will topic
	if (cparam->will_flag != 0 && cparam->pro_ver == PROTOCOL_VERSION_v5) {
		cparam->will_properties = decode_buf_properties(
		    packet, len, &pos, &cparam->will_prop_len, false);
		cparam->will_topic.body =
		    (char *) copy_utf8_str(packet, &pos, &len_of_str);
		cparam->will_topic.len = len_of_str;
		rv                     = len_of_str < 0 ? 1 : 0;
		debug_msg("will_topic: %s %d", cparam->will_topic.body, rv);
		// will msg
		cparam->will_msg.body =
		    (char *) copy_utf8_str(packet, &pos, &len_of_str);
		cparam->will_msg.len = len_of_str;
		rv                   = len_of_str < 0 ? 1 : 0;
		debug_msg("will_msg: %s %d", cparam->will_msg.body, rv);
	}

	// username
	if ((cparam->con_flag & 0x80) > 0) {
		cparam->username.body =
		    (char *) copy_utf8_str(packet, &pos, &len_of_str);
		cparam->username.len = len_of_str;
		rv                   = len_of_str < 0 ? 1 : 0;
		debug_msg(
		    "username: %s %d", cparam->username.body, len_of_str);
	}
	// password
	if ((cparam->con_flag & 0x40) > 0) {
		cparam->password.body =
		    copy_utf8_str(packet, &pos, &len_of_str);
		cparam->password.len = len_of_str;
		rv                   = len_of_str < 0 ? 1 : 0;
		debug_msg(
		    "password: %s %d", cparam->password.body, len_of_str);
	}
	// what if rv = 0?
	if (len + len_of_var + 1 != pos) {
		debug_msg("ERROR in connect handler");
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

	if (cparam->pro_ver == PROTOCOL_VERSION_v5) {
		encode_properties(msg, cparam->properties);
	}

	size_t         msg_len    = nng_msg_len(msg);
	uint8_t        var_len[4] = { 0 };
	struct pos_buf buf = { .curpos = &var_len[0], .endpos = &var_len[4] };

	int     bytes = write_variable_length_value(msg_len, &buf);
	uint8_t cmd   = CMD_CONNACK;
	nng_msg_header_append(msg, &cmd, 1);
	nng_msg_header_append(msg, var_len, bytes);

	print_hex("Header: ", nni_msg_header(msg), nni_msg_header_len(msg));
	print_hex("Body: ", nni_msg_body(msg), nni_msg_len(msg));
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
	cparam->pro_name.len     = 0;
	cparam->pro_name.body    = NULL;
	cparam->clientid.len     = 0;
	cparam->clientid.body    = NULL;
	cparam->will_topic.body  = NULL;
	cparam->will_topic.len   = 0;
	cparam->will_msg.body    = NULL;
	cparam->will_msg.len     = 0;
	cparam->username.body    = NULL;
	cparam->username.len     = 0;
	cparam->password.body    = NULL;
	cparam->password.len     = 0;
	cparam->auth_method.body = NULL;
	cparam->auth_method.len  = 0;
	cparam->auth_data.body   = NULL;
	cparam->auth_data.len    = 0;

	cparam->assignedid = false;

	// MQTT_v5 Variable header
	cparam->session_expiry_interval = 0;
	cparam->rx_max                  = 65535;
	cparam->max_packet_size         = 65535;
	cparam->topic_alias_max         = 0;
	cparam->req_resp_info           = 0;
	cparam->req_problem_info        = 1;
	cparam->user_property.key       = NULL;
	cparam->user_property.len_key   = 0;
	cparam->user_property.val       = NULL;
	cparam->user_property.len_val   = 0;

	// MQTT_v5 Will property ralation
	cparam->will_delay_interval           = 0;
	cparam->payload_format_indicator      = 0;
	cparam->msg_expiry_interval           = 0;
	cparam->content_type.len              = 0;
	cparam->content_type.body             = NULL;
	cparam->resp_topic.len                = 0;
	cparam->resp_topic.body               = NULL;
	cparam->corr_data.body                = NULL;
	cparam->corr_data.len                 = 0;
	cparam->payload_user_property.key     = NULL;
	cparam->payload_user_property.len_key = 0;
	cparam->payload_user_property.val     = NULL;
	cparam->payload_user_property.len_val = 0;
	cparam->prop_len                      = 0;
	cparam->properties                    = NULL;
	cparam->will_prop_len                 = 0;
	cparam->will_properties               = NULL;
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
	debug_msg("destroy conn param");
	nng_free(cparam->pro_name.body, cparam->pro_name.len);
	nng_free(cparam->clientid.body, cparam->clientid.len);
	nng_free(cparam->will_topic.body, cparam->will_topic.len);
	nng_free(cparam->will_msg.body, cparam->will_msg.len);
	nng_free(cparam->username.body, cparam->username.len);
	nng_free(cparam->password.body, cparam->password.len);
	nng_free(cparam->auth_method.body, cparam->auth_method.len);
	nng_free(cparam->auth_data.body, cparam->auth_data.len);
	nng_free(cparam->user_property.key, cparam->user_property.len_key);
	nng_free(cparam->user_property.val, cparam->user_property.len_val);
	nng_free(cparam->content_type.body, cparam->content_type.len);
	nng_free(cparam->resp_topic.body, cparam->resp_topic.len);
	nng_free(cparam->corr_data.body, cparam->corr_data.len);
	nng_free(cparam->payload_user_property.key,
	    cparam->payload_user_property.len_key);
	nng_free(cparam->payload_user_property.val,
	    cparam->payload_user_property.len_val);
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

void
nano_msg_set_dup(nng_msg *msg)
{
	uint8_t *header;

	header  = nni_msg_header(msg);
	*header = *header | 0x08;
}

// alloc a publish msg according to the need
nng_msg *
nano_msg_composer(nng_msg **msgp, uint8_t retain, uint8_t qos,
    mqtt_string *payload, mqtt_string *topic, uint8_t proto_ver)
{
	size_t   rlen;
	uint8_t *ptr, buf[5] = { '\0' };
	uint32_t len;
	nni_msg *msg;

	len = payload->len + topic->len + 2;
	len += proto_ver == PROTOCOL_VERSION_v5 ? 1 : 0;

	msg = *msgp;
	if (msg == NULL) {
		nni_msg_alloc(&msg, len + (qos > 0 ? 2 : 0));
	} else {
		nni_msg_realloc(msg, len + (qos > 0 ? 2 : 0));
	}

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

	if (proto_ver == PROTOCOL_VERSION_v5) {
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

	if (conf->auths.count == 0 || conf->allow_anonymous == true) {
		debug_msg("WARNING: no valid entry in "
		          "etc/nanomq_auth_username.conf.");
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
	nni_msg    *msg = NULL;
	mqtt_string string, topic;
	char        buff[256];
	snprintf(buff, 256, DISCONNECT_MSG, (char *) cparam->username.body,
	    nni_clock(), code, (char *) cparam->clientid.body);
	string.body = buff;
	string.len  = strlen(string.body);
	topic.body  = DISCONNECT_TOPIC;
	topic.len   = strlen(DISCONNECT_TOPIC);
	msg = nano_msg_composer(&msg, 0, 0, &string, &topic, cparam->pro_ver);
	return msg;
}

nng_msg *
nano_msg_notify_connect(conn_param *cparam, uint8_t code)
{
	nni_msg    *msg = NULL;
	mqtt_string string, topic;
	char        buff[256];
	snprintf(buff, 256, CONNECT_MSG, cparam->username.body, nni_clock(),
	    cparam->pro_name.body, cparam->keepalive_mqtt, code,
	    cparam->pro_ver, cparam->clientid.body, cparam->clean_start);
	string.body = buff;
	string.len  = strlen(string.body);
	topic.body  = CONNECT_TOPIC;
	topic.len   = strlen(CONNECT_TOPIC);
	msg = nano_msg_composer(&msg, 0, 0, &string, &topic, cparam->pro_ver);
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
	char         *topic;
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

	if (cparam->pro_ver == PROTOCOL_VERSION_v5) {
		len = get_var_integer(nni_msg_body(msg) + 2, &len_of_varint);
		payload_ptr = nni_msg_body(msg) + 2 + len + len_of_varint;
	} else {
		payload_ptr = nni_msg_body(msg) + 2;
	}
	nni_msg_set_payload_ptr(msg, payload_ptr);
	remain = nni_msg_remaining_len(msg) - 2;

	while (bpos < remain) {
		NNI_GET16(payload_ptr + bpos, len_of_topic);

		if (len_of_topic != 0) {

			debug_msg("The current process topic is %s",
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
		debug_msg("sub topic: %s qos : %x\n", db->topic, db->qos);
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

/**
 * @brief decode puback/pubrec/pubrel/pubcomp
 *
 * @param msg
 * @param packet_id
 * @param reason_code
 * @param proto_ver
 * @return int
 */
int
nmq_pubres_decode(nng_msg *msg, uint16_t *packet_id, uint8_t *reason_code,
    property **prop, uint8_t proto_ver)
{
	int      rv;
	uint8_t *body   = nni_msg_body(msg);
	size_t   length = nni_msg_len(msg);

	struct pos_buf buf = { .curpos = &body[0], .endpos = &body[length] };

	if ((rv = read_uint16(&buf, packet_id)) != MQTT_SUCCESS) {
		return rv;
	}

	if (length == 2 || proto_ver != PROTOCOL_VERSION_v5) {
		return MQTT_SUCCESS;
	}

	if ((rv = read_byte(&buf, reason_code)) != MQTT_SUCCESS) {
		return rv;
	}

	if ((buf.endpos - buf.curpos) <= 0) {
		*prop = NULL;
		return MQTT_SUCCESS;
	}

	uint32_t pos      = (uint32_t) (buf.curpos - body);
	uint32_t prop_len = 0;

	*prop = decode_properties(msg, &pos, &prop_len, false);

	return MQTT_SUCCESS;
}

/**
 * @brief encode header of puback/pubrec/pubrel/pubcomp
 *
 * @param msg
 * @param cmd
 * @return int
 */
int
nmq_pubres_header_encode(nng_msg *msg, uint8_t cmd)
{
	size_t         msg_len    = nng_msg_len(msg);
	uint8_t        var_len[4] = { 0 };
	struct pos_buf buf = { .curpos = &var_len[0], .endpos = &var_len[4] };

	int bytes = write_variable_length_value(msg_len, &buf);

	if (cmd == CMD_PUBREL) {
		cmd |= 0x02;
	}

	nng_msg_header_append(msg, &cmd, 1);
	nng_msg_header_append(msg, var_len, bytes);

	return 0;
}

/**
 * @brief encode puback/pubrec/pubrel/pubcomp
 *
 * @param msg
 * @param packet_id
 * @param reason_code
 * @param prop
 * @param proto_ver
 * @return int
 */
int
nmq_pubres_encode(nng_msg *msg, uint16_t packet_id, uint8_t reason_code,
    property *prop, uint8_t proto_ver)
{
	uint8_t rbuf[2] = { 0 };
	NNI_PUT16(rbuf, packet_id);
	nni_msg_clear(msg);
	nni_msg_append(msg, rbuf, 2);

	if (proto_ver == PROTOCOL_VERSION_v5) {
		if (reason_code == 0 && prop == NULL) {
			return MQTT_SUCCESS;
		}
		nni_msg_append(msg, &reason_code, 1);
		encode_properties(msg, prop);
	}

	return MQTT_SUCCESS;
}