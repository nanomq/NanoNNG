
#ifndef NNG_MQTT_H
#define NNG_MQTT_H

#include "conf.h"
#include "hash_table.h"
#include "nng/mqtt/packet.h"
#include "mqtt.h"
#include "nng/supplemental/util/platform.h"
#include <nng/nng.h>
#include <stdlib.h>

// Do not change to %lu! just supress the warning of compiler!
#define DISCONNECT_MSG          \
	"{\"username\":\"%s\"," \
	"\"ts\":%llu,\"reason_code\":\"%x\",\"client_id\":\"%s\"}"

#define CONNECT_MSG                                                           \
	"{\"username\":\"%s\", "                                              \
	"\"ts\":%llu,\"proto_name\":\"%s\",\"keepalive\":%d,\"return_code\":" \
	"\"%x\",\"proto_ver\":%d,\"client_id\":\"%s\", \"clean_start\":%d}"

#define DISCONNECT_TOPIC "$SYS/brokers/disconnected"

#define CONNECT_TOPIC "$SYS/brokers/connected"

// strip off and return the QoS bits
#define NANO_NNI_LMQ_GET_QOS_BITS(msg) ((size_t) (msg) &0x03)

// strip off and return the msg pointer
#define NANO_NNI_LMQ_GET_MSG_POINTER(msg) \
	((nng_msg *) ((size_t) (msg) & (~0x03)))

// packed QoS bits to the least two significant bits of msg pointer
#define NANO_NNI_LMQ_PACKED_MSG_QOS(msg, qos) \
	((nng_msg *) ((size_t) (msg) | ((qos) &0x03)))

// Variables & Structs
typedef struct pub_extra pub_extra;

// int hex_to_oct(char *str);
// uint32_t htoi(char *str);

extern pub_extra *pub_extra_alloc(pub_extra *);
extern void       pub_extra_free(pub_extra *);
extern uint8_t    pub_extra_get_qos(pub_extra *);
extern uint16_t   pub_extra_get_packet_id(pub_extra *);
extern void       pub_extra_set_qos(pub_extra *, uint8_t);
extern void      *pub_extra_get_msg(pub_extra *);
extern void       pub_extra_set_msg(pub_extra *, void *);
extern void       pub_extra_set_packet_id(pub_extra *, uint16_t);

// MQTT CONNECT
int32_t conn_handler(uint8_t *packet, conn_param *conn_param, size_t max);
int     conn_param_alloc(conn_param **cparam);
void    conn_param_free(conn_param *cparam);
void    conn_param_clone(conn_param *cparam);
int     fixed_header_adaptor(uint8_t *packet, nng_msg *dst);
int     ws_fixed_header_adaptor(uint8_t *packet, nng_msg *dst);

// parser
NNG_DECL uint8_t put_var_integer(uint8_t *dest, uint32_t value);

NNG_DECL uint32_t get_var_integer(const uint8_t *buf, uint32_t *pos);

NNG_DECL int32_t  get_utf8_str(char **dest, const uint8_t *src, uint32_t *pos);
NNG_DECL uint8_t *copy_utf8_str(
    const uint8_t *src, uint32_t *pos, int *str_len);
NNG_DECL uint8_t *copyn_utf8_str(
    const uint8_t *src, uint32_t *pos, uint32_t *str_len, int limit);

// NNG_DECL char *convert_to_utf8(char *src, char *format, size_t *len);
NNG_DECL uint8_t *copyn_str(
    const uint8_t *src, uint32_t *pos, uint32_t *str_len, int limit);

NNG_DECL int utf8_check(const char *str, size_t length);

NNG_DECL uint16_t get_variable_binary(uint8_t **dest, const uint8_t *src);

NNG_DECL uint32_t DJBHash(char *str);
NNG_DECL uint32_t DJBHashn(char *str, uint16_t len);
NNG_DECL uint64_t nano_hash(char *str);
NNG_DECL uint8_t  verify_connect(conn_param *cparam, conf *conf);

// repack
NNG_DECL void inline nano_msg_set_dup(nng_msg *msg);
NNG_DECL nng_msg *nano_pubmsg_composer(nng_msg **, uint8_t retain, uint8_t qos,
    mqtt_string *payload, mqtt_string *topic, uint8_t proto_ver,
    nng_time time);
NNG_DECL nng_msg *nano_dismsg_composer(
    reason_code code, char *rstr, uint8_t *ref, property *prop);
NNG_DECL nng_msg *nano_msg_notify_disconnect(conn_param *cparam, uint8_t code);
NNG_DECL nng_msg *nano_msg_notify_connect(conn_param *cparam, uint8_t code);
NNG_DECL nano_pipe_db *nano_msg_get_subtopic(
    nng_msg *msg, nano_pipe_db *root, conn_param *cparam);
NNG_DECL void nano_msg_free_pipedb(nano_pipe_db *db);
NNG_DECL void nano_msg_ubsub_free(nano_pipe_db *db);
NNG_DECL void nmq_connack_encode(
    nng_msg *msg, conn_param *cparam, uint8_t reason);
NNG_DECL void nmq_connack_session(nng_msg *msg, bool session);

NNG_DECL reason_code check_properties(property *prop);
NNG_DECL property *decode_buf_properties(uint8_t *packet, uint32_t packet_len,
    uint32_t *pos, uint32_t *len, bool copy_value);
NNG_DECL property *decode_properties(
    nng_msg *msg, uint32_t *pos, uint32_t *len, bool copy_value);
NNG_DECL int      encode_properties(nng_msg *msg, property *prop, uint8_t cmd);
NNG_DECL uint32_t get_properties_len(property *prop);
NNG_DECL int      property_free(property *prop);
NNG_DECL property_data *property_get_value(property *prop, uint8_t prop_id);
NNG_DECL void      property_foreach(property *prop, void (*cb)(property *));
NNG_DECL int       property_dup(property **dup, const property *src);
NNG_DECL property *property_pub_by_will(property *will_prop);

NNG_DECL property          *property_alloc(void);
NNG_DECL property_type_enum property_get_value_type(uint8_t prop_id);
NNG_DECL property *property_set_value_u8(uint8_t prop_id, uint8_t value);
NNG_DECL property *property_set_value_u16(uint8_t prop_id, uint16_t value);
NNG_DECL property *property_set_value_u32(uint8_t prop_id, uint32_t value);
NNG_DECL property *property_set_value_varint(uint8_t prop_id, uint32_t value);
NNG_DECL property *property_set_value_binary(
    uint8_t prop_id, uint8_t *value, uint32_t len, bool copy_value);
NNG_DECL property *property_set_value_str(
    uint8_t prop_id, const char *value, uint32_t len, bool copy_value);
NNG_DECL property *property_set_value_strpair(uint8_t prop_id, const char *key,
    uint32_t key_len, const char *value, uint32_t value_len, bool copy_value);
NNG_DECL void      property_append(property *prop_list, property *last);

NNG_DECL int  nmq_pubres_decode(nng_msg *msg, uint16_t *packet_id,
     uint8_t *reason_code, property **prop, uint8_t proto_ver);
NNG_DECL int  nmq_msgack_encode(nng_msg *msg, uint16_t packet_id,
     uint8_t reason_code, property *prop, uint8_t proto_ver);
NNG_DECL int  nmq_pubres_header_encode(nng_msg *msg, uint8_t cmd);
NNG_DECL int  nmq_subinfo_decode(nng_msg *msg, void *l);
NNG_DECL int  nmq_unsubinfo_decode(nng_msg *msg, void *l);
NNG_DECL bool topic_filter(const char *origin, const char *input);
NNG_DECL bool topic_filtern(const char *origin, const char *input, size_t n);

NNG_DECL int nmq_auth_http_connect(conn_param *cparam, conf_auth_http *conf);
NNG_DECL int nmq_auth_http_publish(
    conn_param *cparam, bool is_sub, const char *topic, conf_auth_http *conf);
NNG_DECL int nmq_auth_http_subscribe(conn_param *cparam, bool is_sub,
    topic_queue *topics, conf_auth_http *conf);

#endif // NNG_MQTT_H
