// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
//
// The Struct to store mqtt_packet.

#ifndef NNG_MQTT_PACKET_H
#define NNG_MQTT_PACKET_H

#include "nng/nng.h"
#include "nng/mqtt/mqtt_client.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define UPDATE_FIELD_INT(field, new_obj, old_obj) \
	do {                                      \
		new_obj->field = old_obj->field;  \
	} while (0)

#define UPDATE_FIELD_MQTT_STRING(field, sub_field, new_obj, old_obj)   \
	do {                                                           \
		if (new_obj->field.sub_field == NULL &&                \
		    old_obj->field.sub_field != NULL) {                \
			new_obj->field = old_obj->field;               \
			new_obj->field.sub_field =                     \
			    strdup((char *) old_obj->field.sub_field); \
		}                                                      \
	} while (0)

#define UPDATE_FIELD_MQTT_STRING_PAIR(                                  \
    field, sub_field1, sub_field2, new_obj, old_obj)                    \
	do {                                                            \
		if ((new_obj->field.sub_field1 == NULL &&               \
		        old_obj->field.sub_field1 != NULL) ||           \
		    (new_obj->field.sub_field2 == NULL &&               \
		        old_obj->field.sub_field2 != NULL)) {           \
			new_obj->field = old_obj->field;                \
			new_obj->field.sub_field1 =                     \
			    strdup((char *) old_obj->field.sub_field1); \
			new_obj->field.sub_field2 =                     \
			    strdup((char *) old_obj->field.sub_field2); \
		}                                                       \
	} while (0)

struct mqtt_string {
	char *   body;
	uint32_t len;
};
typedef struct mqtt_string mqtt_string;

struct mqtt_string_node {
	struct mqtt_string_node *next;
	mqtt_string *            it;
};
typedef struct mqtt_string_node mqtt_string_node;

struct mqtt_binary {
	uint8_t *body;
	uint32_t len;
};
typedef struct mqtt_binary mqtt_binary;

struct mqtt_str_pair {
	char *   key; // key
	uint32_t len_key;
	char *   val; // value
	uint32_t len_val;
};
typedef struct mqtt_str_pair mqtt_str_pair;

union Property_type {
	uint8_t  u8;
	uint16_t u16;
	uint32_t u32;
	uint32_t varint;
	mqtt_buf binary;
	mqtt_buf str;
	mqtt_kv  strpair;
};

typedef enum {
	U8,
	U16,
	U32,
	VARINT,
	BINARY,
	STR,
	STR_PAIR,
	UNKNOWN
} property_type_enum;

struct property_data {
	property_type_enum  p_type;
	union Property_type p_value;
	bool                is_copy;
};

typedef struct property_data property_data;

struct property {
	uint8_t          id;
	property_data    data;
	struct property *next;
};
typedef struct property property;

struct mqtt_msg_info {
	uint8_t  qos;
	uint32_t pipe;
	uint8_t  retain;
	uint32_t sub_id;
};
typedef struct mqtt_msg_info mqtt_msg_info;

// variable header in mqtt_packet_subscribe
struct topic_with_option {
	uint8_t     qos : 2;
	uint8_t     no_local : 1;
	uint8_t     rap : 1;
	uint8_t     retain_handling : 2;
	mqtt_string topic_filter;
	uint8_t     reason_code;
};
typedef struct topic_with_option topic_with_option;

struct topic_node {
	topic_with_option *it;
	struct topic_node *next;
};
typedef struct topic_node topic_node;

struct packet_subscribe {
	uint16_t packet_id;
	uint32_t    prop_len;
	property *  properties;
	topic_node *node; // stored topic and option
};
typedef struct packet_subscribe packet_subscribe;

struct packet_unsubscribe {
	uint16_t packet_id;
	uint32_t    prop_len;
	property *  properties;
	topic_node *node; // stored topic and option
};
typedef struct packet_unsubscribe packet_unsubscribe;

#endif
