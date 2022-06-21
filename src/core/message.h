//
// Copyright 2021 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2017 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_MESSAGE_H
#define CORE_MESSAGE_H

#include "nng/mqtt/packet.h"
#include "nng/supplemental/util/platform.h"

// Internally used message API.  Again, this is not part of our public API.
// "trim" operations work from the front, and "chop" work from the end.

extern int      nni_msg_alloc(nni_msg **, size_t);
extern void     nni_msg_free(nni_msg *);
extern int      nni_msg_realloc(nni_msg *, size_t);
extern int      nni_msg_reserve(nni_msg *, size_t);
extern size_t   nni_msg_capacity(nni_msg *);
extern int      nni_msg_dup(nni_msg **, const nni_msg *);
extern void *   nni_msg_header(nni_msg *);
extern size_t   nni_msg_header_len(const nni_msg *);
extern void *   nni_msg_body(nni_msg *);
extern size_t   nni_msg_len(const nni_msg *);
extern int      nni_msg_append(nni_msg *, const void *, size_t);
extern int      nni_msg_insert(nni_msg *, const void *, size_t);
extern int      nni_msg_header_append(nni_msg *, const void *, size_t);
extern int      nni_msg_header_insert(nni_msg *, const void *, size_t);
extern int      nni_msg_trim(nni_msg *, size_t);
extern int      nni_msg_chop(nni_msg *, size_t);
extern void     nni_msg_clear(nni_msg *);
extern void     nni_msg_header_clear(nni_msg *);
extern int      nni_msg_header_trim(nni_msg *, size_t);
extern int      nni_msg_header_chop(nni_msg *, size_t);
extern void     nni_msg_dump(const char *, const nni_msg *);
extern void     nni_msg_header_append_u32(nni_msg *, uint32_t);
extern uint32_t nni_msg_header_trim_u32(nni_msg *);
extern uint32_t nni_msg_trim_u32(nni_msg *);
// Peek and poke variants just access the first uint32 in the
// header.  This is useful when incrementing reference counts, etc.
// It's faster than trim and append, but logically equivalent.
extern uint32_t nni_msg_header_peek_u32(nni_msg *);
extern void     nni_msg_header_poke_u32(nni_msg *, uint32_t);
extern void     nni_msg_set_pipe(nni_msg *, uint32_t);
extern uint32_t nni_msg_get_pipe(const nni_msg *);

// Reference counting messages. This allows the same message to be
// cheaply reused instead of copied over and over again.  Callers of
// this functionality MUST be certain to use nni_msg_unique() before
// passing a message out of their control (e.g. to user programs.)
// Failure to do so will likely result in corruption.
extern void     nni_msg_clone(nni_msg *);
extern nni_msg *nni_msg_unique(nni_msg *);
extern bool     nni_msg_shared(nni_msg *);

// nni_msg_pull_up ensures that the message is unique, and that any
// header present is "pulled up" into the message body.  If the function
// cannot do this for any reason (out of space in the body), then NULL
// is returned.  It is the responsibility of the caller to free the
// original message in that case (same semantics as realloc).
extern nni_msg *nni_msg_pull_up(nni_msg *);

// NANOMQ MQTT
extern nni_time      nni_msg_get_timestamp(nni_msg *m);
extern void          nni_msg_set_timestamp(nni_msg *m, nni_time time);
extern uint8_t       nni_msg_cmd_type(nni_msg *m);
extern uint8_t       nni_msg_get_type(nni_msg *m);
extern uint8_t *     nni_msg_header_ptr(const nni_msg *m);
extern uint8_t *     nni_msg_payload_ptr(const nni_msg *m);
extern uint8_t       nni_msg_get_pub_qos(nni_msg *m);
extern size_t        nni_msg_remaining_len(const nni_msg *m);
extern void          nni_msg_set_payload_ptr(nni_msg *m, uint8_t *ptr);
extern void          nni_msg_set_remaining_len(nni_msg *m, size_t len);
extern void          nni_msg_set_cmd_type(nni_msg *m, uint8_t cmd);
extern void          nni_msg_set_conn_param(nni_msg *m, void *ptr);
extern uint8_t       nni_msg_get_preset_qos(nni_msg *m);
extern uint16_t      nni_msg_get_pub_pid(nni_msg *m);

extern conn_param *nni_msg_get_conn_param(nni_msg *m);

typedef struct conn_propt conn_propt;

struct conn_propt {
	uint8_t session_exp_int[5];
};

struct pipe_db {
	// uint32_t           p_id;
	uint8_t qos;
	char *  topic;
	// conn_param *       conn_param;
	// TODO MQTT5 property
	struct pipe_db *next;
	struct pipe_db *prev;
	struct pipe_db *root;
};

// TODO use ZALLOC later
struct conn_param {
	nni_atomic_int     refcnt;
	uint8_t            pro_ver;
	uint8_t            con_flag;
	uint16_t           keepalive_mqtt;
	uint8_t            clean_start;
	uint8_t            will_flag;
	uint8_t            will_retain;
	uint8_t            will_qos;
	void *             nano_qos_db; // 'sqlite' or 'nni_id_hash_map'
	bool               assignedid;
	struct mqtt_string pro_name;
	struct mqtt_string clientid;
	struct mqtt_string will_topic;
	struct mqtt_string will_msg;
	struct mqtt_string username;
	struct mqtt_binary password;
	// conn_propt    ppt;
	// mqtt_v5
	//nni_time ntime;	// reserve for will msg expiry/delay interval
	// variable header
	uint32_t             session_expiry_interval;
	uint16_t             rx_max;
	uint32_t             max_packet_size;
	uint16_t             topic_alias_max;
	uint8_t              req_resp_info;
	uint8_t              req_problem_info;
	mqtt_buf *           auth_method;
	mqtt_buf *           auth_data;
	mqtt_kv *            user_property;

	nng_time  will_delay_interval;
	uint8_t   payload_format_indicator;
	nng_time  msg_expiry_interval;
	mqtt_buf *content_type;
	mqtt_buf *resp_topic;
	mqtt_buf *corr_data;
	mqtt_kv * payload_user_property;

	uint32_t  prop_len;
	property *properties;
	uint32_t  will_prop_len;
	property *will_properties;
};
// Message protocol private data.  This is specific for protocol use,
// and not exposed to library users.

// nni_proto_msg_ops is used to handle the protocol private data
// associated with a message.
typedef struct nni_proto_msg_ops {
	// This is used to free protocol specific data previously
	// attached to the message, and is called when the message
	// itself is freed, or when protocol private is replaced.
	int (*msg_free)(void *);

	// Duplicate protocol private data when duplicating a message,
	// such as by nni_msg_dup() or calling nni_msg_unique() on a
	// shared message.
	int (*msg_dup)(void **, const void *);
} nni_proto_msg_ops;

// nni_msg_set_proto_data is used to set protocol private data, and
// callbacks for freeing and duplicating said data, on the message.
// If other protocol private data exists on the message, it will be freed.
// NULL can be used for the ops and the pointer to clear any previously
// set data. The message must not be shared when this is called.
extern void nni_msg_set_proto_data(nng_msg *, nni_proto_msg_ops *, void *);

extern void *nni_msg_get_proto_data(nng_msg *m);

// nni_msg_get_proto_data returns the data previously set on the message.
// Note that the protocol is responsible for ensuring that the data on
// the message is set by it alone.
extern void *nni_msg_get_proto_data(nng_msg *);

extern char *nni_msg_get_pub_topic(nng_msg *, int *topic_len);

extern uint8_t nni_msg_get_pub_qos(nng_msg *m);

#endif // CORE_SOCKET_H
