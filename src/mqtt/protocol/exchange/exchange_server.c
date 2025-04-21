// Copyright 2023 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include <inttypes.h>

#include "core/nng_impl.h"
#include "nng/protocol/mqtt/mqtt.h"
#include "nng/protocol/mqtt/mqtt_parser.h"
#include "nng/mqtt/mqtt_client.h"
#include "nng/exchange/exchange.h"
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/stream/stream.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "nng/protocol/reqrep0/rep.h"
#if defined(SUPP_PARQUET)
#include "nng/supplemental/nanolib/parquet.h"
#endif
#include "nng/supplemental/nanolib/blf.h"

#define NANO_MAX_MQ_BUFFER_LEN 1024

#ifndef NNI_PROTO_EXCHANGE_V0
#define NNI_PROTO_EXCHANGE_V0 NNI_PROTO(15, 0)
#endif

typedef struct exchange_sock_s         exchange_sock_t;
typedef struct exchange_node_s         exchange_node_t;
typedef struct exchange_pipe_s         exchange_pipe_t;

static nng_aio *query_reset_aio = NULL;
static nng_atomic_int *query_limit = NULL;

// one MQ, one Sock(TBD), one PIPE
struct exchange_pipe_s {
	nni_pipe        *pipe;
	exchange_sock_t *sock;
	bool             closed;
	uint32_t         id;
};

// struct exchange_ctx {
// 	nni_list_node    node;
// 	sub0_sock       *sock;
// 	exchange_pipe_t *pipe;
// 	nni_list         topics;     // TODO: Consider patricia trie
// 	nni_list         recv_queue; // can have multiple pending receives
// };

//TODO replace it with pipe
struct exchange_node_s {
	exchange_t      *ex;
	exchange_sock_t *sock;
};

struct exchange_sock_s {
	nni_mtx          mtx;
	bool             isBusy;
	nni_atomic_bool  closed;
	nni_id_map       rbmsgmap;
	exchange_node_t *ex_node;
	nng_socket      *pair0_sock;
	nni_aio          query_aio;
	nni_lmq          lmq;
};


static void exchange_sock_init(void *arg, nni_sock *sock);
static void exchange_sock_fini(void *arg);
static void exchange_sock_open(void *arg);
static void exchange_sock_send(void *arg, nni_aio *aio);
static void exchange_sock_recv(void *arg, nni_aio *aio);

static int
exchange_add_ex(exchange_sock_t *s, exchange_t *ex)
{
	nni_mtx_lock(&s->mtx);

	if (s->ex_node != NULL) {
		log_error("exchange client add exchange failed! ex_node is not NULL!\n");
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	exchange_node_t *node;
	node = (exchange_node_t *)nng_alloc(sizeof(exchange_node_t));
	if (node == NULL) {
		log_error("exchange client add exchange failed! No memory!\n");
		nni_mtx_unlock(&s->mtx);
		return -1;
	}

	node->ex = ex;
	node->sock = s;

	s->ex_node = node;
	nni_mtx_unlock(&s->mtx);
	return 0;
}

static void query_send_eof(nng_socket *sock, nng_aio *aio)
{
	NNI_ARG_UNUSED(aio);
	nng_msg *msg = NULL;
	nng_msg_alloc(&msg, 0);

	unsigned char eof[2];
	eof[0] = 0x0B;
	eof[1] = 0xAD;

	nng_msg_append(msg, eof, 2);
	nng_sendmsg(*sock, msg, 0);

	return;
}

static inline void parquet_data_ret_free(struct parquet_data_ret *parquet_data)
{
	if (parquet_data == NULL) {
		return;
	}
	if (parquet_data->payload_arr != NULL) {
		for (uint32_t j = 0; j < parquet_data->col_len; j++) {
			if (parquet_data->payload_arr[j] != NULL) {
				for (uint32_t k = 0; k < parquet_data->row_len; k++) {
					if (parquet_data->payload_arr[j][k] != NULL) {
						if (parquet_data->payload_arr[j][k]->data != NULL && parquet_data->payload_arr[j][k]->size > 0) {
							nng_free(parquet_data->payload_arr[j][k]->data, parquet_data->payload_arr[j][k]->size);
						}
						nng_free(parquet_data->payload_arr[j][k], parquet_data->payload_arr[j][k]->size);
					}
				}
				nng_free(parquet_data->payload_arr[j], sizeof(parquet_data_packet *) * parquet_data->row_len);
			}
		}
		nng_free(parquet_data->payload_arr, sizeof(parquet_data_packet **) * parquet_data->col_len);
	}

	if (parquet_data->schema != NULL) {
		for (uint32_t i = 0; i < parquet_data->col_len; i++) {
			if (parquet_data->schema[i] != NULL) {
				nng_free(parquet_data->schema[i], strlen(parquet_data->schema[i]));
			}
		}
		nng_free(parquet_data->schema, sizeof(char *) * parquet_data->col_len);
	}

	if (parquet_data->ts != NULL) {
		nng_free(parquet_data->ts, sizeof(uint64_t) * parquet_data->row_len);
	}

	nng_free(parquet_data, sizeof(struct parquet_data_ret));

	return;
}

static inline void parquet_datas_ret_free(parquet_data_ret **parquet_datas, uint32_t size)
{
	if (parquet_datas == NULL) {
		return;
	}
	for (uint32_t i = 0; i < size; i ++) {
		parquet_data_ret *parquet_data = parquet_datas[i];
		parquet_data_ret_free(parquet_data);
	}

	nng_free(parquet_datas, sizeof(parquet_data_ret *) * size);

	return;
}

static inline void ringbus_stream_data_in_free(struct stream_data_in *stream_data_in)
{
	if (stream_data_in == NULL) {
		return;
	}
	if (stream_data_in->keys != NULL) {
		nng_free(stream_data_in->keys, sizeof(uint64_t) * stream_data_in->len);
	}
	if (stream_data_in->datas != NULL) {
		nng_free(stream_data_in->datas, sizeof(void *) * stream_data_in->len);
	}
	if (stream_data_in->lens != NULL) {
		nng_free(stream_data_in->lens, sizeof(uint32_t) * stream_data_in->len);
	}
	nng_free(stream_data_in, sizeof(struct stream_data_in));

	return;
}

static struct stream_data_in *ringbus_stream_data_in_init(uint32_t count, nng_msg **msgList)
{
	uint32_t diff = 0;
	struct stream_data_in *stream_data = NULL;

	if (msgList == NULL || count == 0) {
		return NULL;
	}

	stream_data = nng_alloc(sizeof(struct stream_data_in));
	if (stream_data == NULL) {
		log_error("Failed to allocate memory for stream_data\n");
		return NULL;
	}
	stream_data->len = count;
	stream_data->datas = nng_alloc(sizeof(void *) * count);
	if (stream_data->datas == NULL) {
		log_error("Failed to allocate memory for stream_data->datas\n");
		nng_free(stream_data, sizeof(struct stream_data_in));
		return NULL;
	}
	stream_data->keys = nng_alloc(sizeof(uint64_t) * count);
	if (stream_data->keys == NULL) {
		log_error("Failed to allocate memory for stream_data->keys\n");
		nng_free(stream_data->datas, sizeof(void *) * count);
		nng_free(stream_data, sizeof(struct stream_data_in));
		return NULL;
	}

	stream_data->lens = nng_alloc(sizeof(uint32_t) * count);
	if (stream_data->lens == NULL) {
		log_error("Failed to allocate memory for stream_data->lens\n");
		nng_free(stream_data->keys, sizeof(uint64_t) * count);
		nng_free(stream_data->datas, sizeof(void *) * count);
		nng_free(stream_data, sizeof(struct stream_data_in));
		return NULL;
	}

	for (uint32_t i = 0; i < count; i++) {
		diff = nng_msg_len(msgList[i]) -
			((uintptr_t)nng_msg_payload_ptr(msgList[i]) - (uintptr_t)nng_msg_body(msgList[i]));
		stream_data->lens[i] = diff;
		stream_data->datas[i] = nng_msg_payload_ptr(msgList[i]);
		stream_data->keys[i] = nni_msg_get_timestamp(msgList[i]);
	}

	return stream_data;
}

static struct parquet_data_ret *ringbus_parquet_data_ret_init(struct stream_data_out *stream_data_out, struct cmd_data *cmd_data)
{
	uint32_t new_index = 0;
	uint32_t new_col_len = 0;
	struct parquet_data_ret *parquet_data_ret = NULL;

	if (stream_data_out == NULL || cmd_data == NULL) {
		log_error("stream_data_out or cmd_data is null");
		return NULL;
	}

	parquet_data_ret = nng_alloc(sizeof(struct parquet_data_ret));
	if (parquet_data_ret == NULL) {
		log_error("Failed to allocate memory for parquet_data_ret\n");
		return NULL;
	}

	for (uint32_t i = 1; i < stream_data_out->col_len; i++) {
		for (uint32_t j = 0; j < cmd_data->schema_len; j++) {
			if (strcmp(stream_data_out->schema[i], cmd_data->schema[j]) == 0) {
				new_col_len++;
				break;
			}
		}
	}
	if (new_col_len == 0) {
		log_error("new_col_len is 0");
		nng_free(parquet_data_ret, sizeof(struct parquet_data_ret));
		return NULL;
	}

	parquet_data_ret->col_len = new_col_len;
	parquet_data_ret->row_len = stream_data_out->row_len;
	parquet_data_ret->payload_arr = nng_alloc(sizeof(parquet_data_packet *) * new_col_len);
	parquet_data_ret->schema = nng_alloc(sizeof(char *) * new_col_len);
	parquet_data_ret->ts = nng_alloc(sizeof(uint64_t) * stream_data_out->row_len);
	memcpy(parquet_data_ret->ts, stream_data_out->ts, sizeof(uint64_t) * stream_data_out->row_len);

	for (uint32_t i = 0; i < stream_data_out->col_len; i++) {
		for (uint32_t j = 0; j < cmd_data->schema_len; j++) {
			if (strcmp(stream_data_out->schema[i], cmd_data->schema[j]) == 0) {
				parquet_data_ret->schema[new_index] = stream_data_out->schema[i];
				parquet_data_ret->payload_arr[new_index] = stream_data_out->payload_arr[i];
				new_index++;
				break;
			}
		}
	}

	return parquet_data_ret;
}

static struct stream_decoded_data *fuzz_search_result_cat(nng_msg **msgList,
														 uint32_t count,
														 uint8_t stream_type,
														 struct cmd_data *cmd_data)
{
	struct stream_data_in *stream_data = NULL;
	struct stream_data_out *stream_data_out = NULL;
	struct stream_decoded_data *decoded_data = NULL;

	if (msgList == NULL || count == 0) {
		return NULL;
	}

	stream_data = ringbus_stream_data_in_init(count, msgList);
	if (stream_data == NULL) {
		log_error("Failed to allocate memory for stream_data\n");
		return NULL;
	}

	stream_data_out = stream_encode(stream_type, stream_data);
	if (stream_data_out == NULL) {
		log_error("stream_encode failed!");
		ringbus_stream_data_in_free(stream_data);
		return NULL;
	}

	parquet_data_ret *parquet_data_ele = ringbus_parquet_data_ret_init(stream_data_out, cmd_data);
	if (parquet_data_ele == NULL) {
		log_error("ringbus_parquet_data_ret_init failed!");
		parquet_data_free((parquet_data *)stream_data_out);
		ringbus_stream_data_in_free(stream_data);
		return NULL;
	}

	decoded_data = stream_decode(stream_type, parquet_data_ele);
	if (decoded_data == NULL) {
		log_error("stream_decode failed!");
		parquet_data_ret_free(parquet_data_ele);
		parquet_data_free((parquet_data *)stream_data_out);
		ringbus_stream_data_in_free(stream_data);
		return NULL;
	}

	ringbus_stream_data_in_free(stream_data);
	parquet_data_free((parquet_data *)stream_data_out);

	nng_free(parquet_data_ele->schema, sizeof(char *) * parquet_data_ele->col_len);
	nng_free(parquet_data_ele->payload_arr, sizeof(parquet_data_packet *) * parquet_data_ele->col_len);
	nng_free(parquet_data_ele->ts, sizeof(uint64_t) * parquet_data_ele->row_len);
	nng_free(parquet_data_ele, sizeof(struct parquet_data_ret));

	return decoded_data;
}

static void query_send_sync(exchange_sock_t *s, struct cmd_data *cmd_data)
{
	int ret = 0;
	uint32_t count = 0;
	nng_msg **msgList = NULL;
	struct stream_decoded_data *ringbus_decoded_data = NULL;

	ret = exchange_client_get_msgs_fuzz(s, cmd_data->start_key, cmd_data->end_key, &count, &msgList);
	if (ret == 0 && count != 0 && msgList != NULL) {
		ringbus_decoded_data = fuzz_search_result_cat(msgList, count, s->ex_node->ex->streamType, cmd_data);
		if (ringbus_decoded_data == NULL) {
			log_error("get decoded data from ringbus failed!");
		}
		nng_free(msgList, sizeof(nng_msg *) * count);
	} else {
		log_warn("exchange_client_get_msgs_fuzz failed! count: %d", count);
	}

#if defined(SUPP_PARQUET)
	uint32_t parquet_decoded_data_len = 0;
	struct stream_decoded_data **parquet_decoded_data = NULL;
	parquet_data_ret **parquet_datas = NULL;
	parquet_filename_range file_range;

	file_range.filename = NULL;
	file_range.keys[0] = cmd_data->start_key;
	file_range.keys[1] = cmd_data->end_key;
	parquet_datas = parquet_get_data_packets_in_range_by_column(&file_range,
																s->ex_node->ex->topic,
																(const char **)cmd_data->schema,
																cmd_data->schema_len,
																&parquet_decoded_data_len);
	if (parquet_datas != NULL && parquet_decoded_data_len > 0) {
		parquet_decoded_data = nng_alloc(sizeof(struct stream_decoded_data *) * parquet_decoded_data_len);
		for (uint32_t i = 0; i < parquet_decoded_data_len; i++) {
			if (parquet_datas[i] != NULL) {
				parquet_decoded_data[i] = stream_decode(s->ex_node->ex->streamType, parquet_datas[i]);
			} else {
				parquet_decoded_data[i] = NULL;
			}
		}
		parquet_datas_ret_free(parquet_datas, parquet_decoded_data_len);
	}
#endif
#if defined(SUPP_BLF)
	/* not support */
	const char **blf_fnames = NULL;
	uint32_t     blf_sz     = 0;
	/* blf not support fuzz search now */
	blf_fnames = blf_find_span(startKey, endKey, &blf_sz);
	if (blf_fnames && blf_sz > 0) {
		ret = get_persistence_files(blf_sz, (char **) blf_fnames, obj);
		if (ret != 0) {
			log_error("get_blf_files failed!");
		}
	} else {
		log_error("blf_find_span failed! sz: %d", blf_sz);
	}
#endif

	nng_msg *msg = NULL;
	nng_msg_alloc(&msg, 0);
#if defined(SUPP_PARQUET)
	if (parquet_decoded_data != NULL && parquet_decoded_data_len > 0) {
		for (uint32_t i = 0; i < parquet_decoded_data_len; i++) {
			if (parquet_decoded_data[i] != NULL && parquet_decoded_data[i]->len > 0) {
				log_info("parquet_decoded_data[%d] size: %d", i, parquet_decoded_data[i]->len);
				nng_msg_append(msg, parquet_decoded_data[i]->data, parquet_decoded_data[i]->len);
				stream_decoded_data_free(parquet_decoded_data[i]);
			} else {
				log_info("parquet_decoded_data[%d] is NULL", i);
			}
		}
		nng_free(parquet_decoded_data, sizeof(struct stream_decoded_data *) * parquet_decoded_data_len);
	}
#endif

	if (ringbus_decoded_data != NULL && ringbus_decoded_data->len > 0) {
		log_info("ringbus_decoded_data size: %d", ringbus_decoded_data->len);
		nng_msg_append(msg, ringbus_decoded_data->data, ringbus_decoded_data->len);
		stream_decoded_data_free(ringbus_decoded_data);
	}

	nng_sendmsg(*(s->pair0_sock), msg, 0);

	return;
}

static void query_send_async(exchange_sock_t *s, struct cmd_data *cmd_data)
{
	uint32_t count = 0;
	int ret = 0;
	nng_msg **msgList = NULL;
	struct stream_decoded_data *ringbus_decoded_data = NULL;

#if defined(SUPP_PARQUET)
	parquet_filename_range **file_ranges = NULL;

	file_ranges = parquet_get_file_ranges(cmd_data->start_key, cmd_data->end_key, s->ex_node->ex->topic);
	if (file_ranges != NULL) {
		uint32_t size = 0;
		uint32_t file_range_idx = 0;
		parquet_filename_range *file_range = NULL;

		file_range = file_ranges[file_range_idx++];
		if (file_range == NULL) {
			log_error("file range is null");
		}
		while (file_range != NULL) {
			parquet_data_ret **parquet_datas = NULL;

			parquet_datas = parquet_get_data_packets_in_range_by_column(file_range,
																		s->ex_node->ex->topic,
																		(const char **)cmd_data->schema,
																		cmd_data->schema_len,
																		&size);
			if (parquet_datas != NULL) {
				if (size <= 1)
					log_warn("parquet_datas size: %d", size);
				for (uint32_t i = 0; i < size; i++) {
					if (parquet_datas[i] != NULL) {
						struct stream_decoded_data *parquet_decoded_data = NULL;

						parquet_decoded_data = stream_decode(s->ex_node->ex->streamType, parquet_datas[i]);
						if (parquet_decoded_data != NULL && parquet_decoded_data->len > 0) {
							log_info("parquet_decoded_data[%d] size: %d", i, parquet_decoded_data->len);
							nng_msg *newmsg = NULL;
							nng_msg_alloc(&newmsg, 0);
							nni_msg_append(newmsg, parquet_decoded_data->data, parquet_decoded_data->len);
							if (s->pair0_sock)
								nng_sendmsg(*(s->pair0_sock), newmsg, 0);
							else
								log_error("pair0_sock is null!!!!!!!!!");
							/* NOTE: sleep 1000ms */
							nng_msleep(1000);
							stream_decoded_data_free(parquet_decoded_data);
						} else {
							if (parquet_decoded_data == NULL) {
								log_warn("parquet_decoded_data is null");
							} else {
								log_warn("parquet_decoded_data len : %d", parquet_decoded_data->len);
							}
						}
					} else {
						log_error("parquet_datas[%d] is NULL", i);
					}
				}
				parquet_datas_ret_free(parquet_datas, size);
			} else {
				log_error("parquet_get_data_packets_in_range_by_column failed!");
			}
			nng_free((void *)file_range->filename, strlen(file_range->filename));
			nng_free(file_range, sizeof(parquet_filename_range));
			file_range = file_ranges[file_range_idx++];
		}
		nng_free(file_ranges, sizeof(parquet_filename_range *) * file_range_idx);
	} else {
		log_error("parquet_find_file_range %ld~%ld failed!",
				cmd_data->start_key, cmd_data->end_key);
	}
#endif

	ret = exchange_client_get_msgs_fuzz(s, cmd_data->start_key, cmd_data->end_key, &count, &msgList);
	if (ret == 0 && count != 0 && msgList != NULL) {
		ringbus_decoded_data = fuzz_search_result_cat(msgList, count, s->ex_node->ex->streamType, cmd_data);
		if (ringbus_decoded_data == NULL) {
			log_error("fuzz_search_result_cat failed!");
		}
		nng_free(msgList, sizeof(nng_msg *) * count);
	} else {
		log_warn("ringbus failed! count: %d", count);
	}

	if (ringbus_decoded_data != NULL && ringbus_decoded_data->len > 0) {
		log_info("ringbus_decoded_data size: %d", ringbus_decoded_data->len);
		nng_msg *newmsg = NULL;
		nng_msg_alloc(&newmsg, 0);
		nni_msg_append(newmsg, ringbus_decoded_data->data, ringbus_decoded_data->len);
		nng_sendmsg(*(s->pair0_sock), newmsg, 0);
		/* NOTE: sleep 1000ms */
		nng_msleep(1000);
		stream_decoded_data_free(ringbus_decoded_data);
	}

	return;
}

static void cmd_data_free(struct cmd_data *cmd_data)
{
	if (cmd_data != NULL) {
		if (cmd_data->schema != NULL) {
			for (uint32_t i = 0; i < cmd_data->schema_len; i++) {
				if (cmd_data->schema[i] != NULL) {
					nng_free(cmd_data->schema[i], strlen(cmd_data->schema[i]));
				}
			}
			nng_free(cmd_data->schema, sizeof(char *) * cmd_data->schema_len);
		}
		nng_free(cmd_data, sizeof(struct cmd_data));
	}

	return;
}

static void
query_cb(void *arg)
{
	int rv = 0;
	exchange_sock_t *s = arg;

	nni_aio *aio = &s->query_aio;
	nng_msg *msg = nng_aio_get_msg(aio);
	if (msg == NULL) {
		nng_recv_aio(*(s->pair0_sock), aio);
		log_error("NUll Commanding msg!");
		return;
	}

	if (query_limit != NULL) {
		rv = nng_atomic_dec_nv(query_limit);
		if (rv < 0) {
			log_warn("Hook searching too frequently");
			query_send_eof(s->pair0_sock, &s->query_aio);
			nng_recv_aio(*(s->pair0_sock), aio);
			nng_msg_free(msg);
			return;
		}
	}

	if (s->ex_node == NULL || s->ex_node->ex == NULL || s->ex_node->ex->topic[0] == 0) {
		query_send_eof(s->pair0_sock, &s->query_aio);
		nng_recv_aio(*(s->pair0_sock), aio);
		log_error("exchange_sock_t is not ready!");
		nng_msg_free(msg);
		return;
	}
	char *topic = s->ex_node->ex->topic;

	char *keystr = (char *)nng_msg_body(msg);
	if (keystr == NULL) {
		query_send_eof(s->pair0_sock, &s->query_aio);
		nng_recv_aio(*(s->pair0_sock), aio);
		log_error("error in parsing keystr");
		nng_msg_free(msg);
		return;
	}

	log_info("Recv command: %s topic: %s", keystr, topic);
	struct cmd_data *cmd_data = stream_cmd_parser(s->ex_node->ex->streamType, keystr);
	if (cmd_data == NULL) {
		log_error("stream_cmd_parser failed!");
		query_send_eof(s->pair0_sock, &s->query_aio);
		nng_recv_aio(*(s->pair0_sock), aio);
		nng_msg_free(msg);
		return;
	}

	if (cmd_data->is_sync) {
		query_send_sync(s, cmd_data);
	} else {
		query_send_async(s, cmd_data);
	}

	query_send_eof(s->pair0_sock, &s->query_aio);
	nng_recv_aio(*(s->pair0_sock), aio);
	cmd_data_free(cmd_data);
	nng_msg_free(msg);

	return;
}

#define QUERY_RESET_DURATION 5

static void query_reset_cb(void *arg)
{
	uint32_t *query_limit_conf = (uint32_t *)arg;

	nng_duration duration;

	duration = QUERY_RESET_DURATION * 1000;

	nng_atomic_set(query_limit, *query_limit_conf);

	nng_sleep_aio(duration, query_reset_aio);

	return;
}

static void
exchange_sock_init(void *arg, nni_sock *sock)
{
	NNI_ARG_UNUSED(sock);
	exchange_sock_t *s = arg;

	nni_atomic_init_bool(&s->closed);
	nni_atomic_set_bool(&s->closed, false);
	nni_mtx_init(&s->mtx);
	nni_id_map_init(&s->rbmsgmap, 0, 0, true);
	s->isBusy = false;
	s->pair0_sock = NULL;

	nni_lmq_init(&s->lmq, 256);

	nni_aio_init(&s->query_aio, query_cb, s);


	return;
}

static void
exchange_sock_fini(void *arg)
{
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node;
//	nng_msg *msg;

	ex_node = s->ex_node;

//	if ((msg = nni_aio_get_msg(&s->ex_aio)) != NULL) {
//		nni_aio_set_msg(&s->ex_aio, NULL);
//		nni_msg_free(msg);
//	}

	nni_aio_fini(&s->query_aio);

	exchange_release(ex_node->ex);

	nni_free(ex_node, sizeof(*ex_node));
	ex_node = NULL;

	nni_id_map_fini(&s->rbmsgmap);
	nni_lmq_fini(&s->lmq);
	nni_mtx_fini(&s->mtx);

	return;
}

static void
exchange_sock_open(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return;
}

static void
exchange_sock_close(void *arg)
{
	exchange_sock_t *s = arg;

	nni_atomic_set_bool(&s->closed, true);
	nni_aio_stop(&s->query_aio);
	nni_aio_close(&s->query_aio);

	return;
}

/* Check if the msg is already in rbmsgmap, if not, add it to rbmsgmap */
static inline int
exchange_client_handle_msg(exchange_node_t *ex_node, nni_msg *msg, nni_aio *aio)
{
	int ret = 0;
	uint64_t key;
	nni_msg *tmsg = NULL;

	key  = nni_msg_get_timestamp(msg);
	nni_aio_set_prov_data(aio, NULL);

	tmsg = nni_id_get(&ex_node->sock->rbmsgmap, key);
	if (tmsg != NULL) {
		log_error("[%s]msg already in rbmsgmap, overwirte is not allowed", ex_node->ex->topic);
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = nni_id_set(&ex_node->sock->rbmsgmap, key, msg);
	if (ret != 0) {
		log_error("[%s]rbmsgmap set failed", ex_node->ex->topic);
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = exchange_handle_msg(ex_node->ex, key, msg, aio);
	if (ret != 0) {
		log_error("[%s]exchange_handle_msg failed!\n", ex_node->ex->topic);
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}
	nng_msg **msgs = nng_aio_get_prov_data(aio);
	if (msgs != NULL) {
		/* Clean up rbmsgmap */
		nng_msg *tmsg = nng_aio_get_msg(aio);
		int *msgs_lenp = (int *)nng_msg_get_proto_data(tmsg);
		if (msgs_lenp != NULL) {
			for (int i = 0; i < *msgs_lenp; i++) {
				if (msgs[i] != NULL) {
					uint64_t tkey = nni_msg_get_timestamp(msgs[i]);
					nni_id_remove(&ex_node->sock->rbmsgmap, tkey);
				}
			}
		}
	}

	return 0;
}

static inline void
exchange_do_send(exchange_node_t *ex_node, nni_msg *msg, nni_aio *user_aio)
{
	int ret = 0;

	exchange_sock_t *s = ex_node->sock;
	if (nni_atomic_get_bool(&s->closed)) {
		// This occurs if the mqtt_pipe_close has been called.
		// In that case we don't want any more processing.
		log_error("[%s]exchange sock is closed!", ex_node->ex->topic);
		nni_aio_finish_error(user_aio, NNG_EINVAL);
		return;
	}

	ret = exchange_client_handle_msg(ex_node, msg, user_aio);
	if (ret != 0) {
		log_error("[%s]exchange handle msg failed%d!", ex_node->ex->topic, ret);
		nni_aio_finish_error(user_aio, NNG_EINVAL);
	} else {
		nng_msg *tmsg = nng_aio_get_msg(user_aio);
		if (tmsg != NULL) {
			char *topic = ex_node->ex->topic;
			nng_msg_set_conn_param(tmsg, topic);
			nng_msg_set_cmd_type(tmsg, ex_node->ex->streamType);
			nng_msg_set_payload_ptr(tmsg, (uint8_t *)ex_node->ex->chunk_size);
			nni_aio_set_msg(user_aio, tmsg);
		}
		nni_aio_finish(user_aio, 0, 0);
	}

	return;
}

static void
exchange_sock_send(void *arg, nni_aio *aio)
{
	nni_msg         *msg = NULL;
	exchange_node_t *ex_node = NULL;
	exchange_sock_t *s = arg;

	if (nni_aio_begin(aio) != 0) {
		log_error("reuse aio in exchanging!");
		return;
	}

	msg = nni_aio_get_msg(aio);
	nni_aio_set_msg(aio, NULL);
	if (msg == NULL || nni_msg_get_type(msg) != CMD_PUBLISH) {
		if (msg == NULL)
			log_error("Get null msg!");
		else
			log_error("Invalid msg type %d!", nni_msg_get_type(msg));
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}

	nni_mtx_lock(&s->mtx);
	if (s->ex_node == NULL) {
		log_error("NULL s->ex_node!");
		nni_aio_finish_error(aio, NNG_EINVAL);
		nni_mtx_unlock(&s->mtx);
		return;
	}

	ex_node = s->ex_node;
	exchange_do_send(ex_node, msg, aio);
	nni_mtx_unlock(&s->mtx);
	return;
}

/**
 * For exchanger, sock_recv is meant for consuming msg from MQ actively
*/
static void
exchange_sock_recv(void *arg, nni_aio *aio)
{
	int ret = 0;
	uint64_t key = 0;
	uint64_t startKey = 0;
	uint64_t endKey = 0;
	uint32_t count = 0;
	nni_msg *msg = NULL;
	nng_msg **list = NULL;
	exchange_sock_t *s = arg;

	if (nni_aio_begin(aio) != 0) {
		log_error("reuse aio in exchanging!");
		return;
	}
	nni_mtx_lock(&s->mtx);
	msg = nni_aio_get_msg(aio);
	if (msg == NULL) {
		log_error("NUll Commanding msg!");
		nni_mtx_unlock(&s->mtx);
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}

	nni_aio_set_prov_data(aio, NULL);
	nni_aio_set_msg(aio, NULL);

	nng_time *tss = (nng_time *)nni_msg_get_proto_data(msg);
	if (tss == NULL) {
		key = nni_msg_get_timestamp(msg);

		count = 1;
		ret = exchange_client_get_msgs_by_key(s, key, count, &list);
		if (ret != 0) {
			log_warn("exchange_client_get_msgs_by_key failed!");
			nni_mtx_unlock(&s->mtx);
			nni_aio_finish_error(aio, NNG_EINVAL);
			return;
		}
	} else {
		if (tss[2] == 0) {
			/* fuzz search */
			startKey = tss[0];
			endKey = tss[1];

			ret = exchange_client_get_msgs_fuzz(s, startKey, endKey, &count, &list);
			if (ret != 0) {
				log_warn("exchange_client_get_msgs_fuzz failed!");
				nni_mtx_unlock(&s->mtx);
				nni_aio_finish_error(aio, NNG_EINVAL);
				return;
			}
		} else if (tss[2] == 1) {
			/* clean up and return */
			/* Only one exchange with one ringBuffer now */
			nng_mtx_lock(s->ex_node->ex->rbs[0]->ring_lock);
			ret = ringBuffer_get_and_clean_msgs(s->ex_node->ex->rbs[0], &count, &list);
			if (ret != 0) {
				log_warn("ringBuffer_get_and_clean_msgs failed!");
				nng_mtx_unlock(s->ex_node->ex->rbs[0]->ring_lock);
				nni_mtx_unlock(&s->mtx);
				nni_aio_finish_error(aio, NNG_EINVAL);
				return;
			}
			nng_mtx_unlock(s->ex_node->ex->rbs[0]->ring_lock);
		} else if (tss[2] == 2) {
			/* Change MQ fullOp to tss[1] */
			/* Only one exchange with one ringBuffer now */
			nng_mtx_lock(s->ex_node->ex->rbs[0]->ring_lock);
			ret = ringBuffer_set_fullOp(s->ex_node->ex->rbs[0], tss[1]);
			if (ret != 0) {
				log_warn("ringBuffer_fullOp failed!");
				nng_mtx_unlock(s->ex_node->ex->rbs[0]->ring_lock);
				nni_mtx_unlock(&s->mtx);
				nni_aio_finish_error(aio, NNG_EINVAL);
				return;
			}
			nng_mtx_unlock(s->ex_node->ex->rbs[0]->ring_lock);
		}
	}

	nni_aio_set_msg(aio, (void *)list);
	nni_aio_set_prov_data(aio, (void *)(uintptr_t)count);
	nni_mtx_unlock(&s->mtx);
	nni_aio_finish(aio, 0, 0);

	return;
}

static int
exchange_sock_bind_exchange(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	exchange_sock_t *s = arg;
	int rv;

	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);

	conf_exchange_node * node = *(conf_exchange_node **) v;

	char    **rbsName = NULL;
	uint32_t *rbsCap = NULL;
	uint8_t  *rbsFullOp = NULL;
	for (int i = 0; i<(int)node->rbufs_sz; ++i) {
		cvector_push_back(rbsName, node->rbufs[i]->name);
		cvector_push_back(rbsCap, node->rbufs[i]->cap);
		cvector_push_back(rbsFullOp, node->rbufs[i]->fullOp);
	}

	exchange_t *ex = NULL;
	rv = exchange_init(&ex, node->name, node->topic, node->streamType, node->chunk_size,
			rbsCap, rbsName, rbsFullOp, cvector_size(rbsName));

	cvector_free(rbsName);
	cvector_free(rbsCap);
	cvector_free(rbsFullOp);
	if (rv != 0) {
		log_error("Failed to exchange_init %d", rv);
		return rv;
	}

	rv = exchange_add_ex(s, ex);

	return (rv);
}

static int
exchange_sock_get_rbmsgmap(void *arg, void *v, size_t *szp, nni_opt_type t)
{
	exchange_sock_t *s = arg;
	int              rv;

	nni_mtx_lock(&s->mtx);
	rv = nni_copyout_ptr(&s->rbmsgmap, v, szp, t);
	nni_mtx_unlock(&s->mtx);
	return (rv);
}

static int
exchange_sock_start_limit_timer(void *arg, const void *v, size_t sz, nni_opt_type t)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(sz);
	NNI_ARG_UNUSED(t);
	conf_exchange_node *node = *(conf_exchange_node **)v;

	nng_atomic_alloc(&query_limit);

	nni_aio_alloc(&query_reset_aio, query_reset_cb, &node->limit_frequency);

	nng_aio_finish(query_reset_aio, 0);

	return 0;
}

int
exchange_client_get_msg_by_key(void *arg, uint64_t key, nng_msg **msg)
{
	exchange_sock_t *s = arg;
	nni_id_map *rbmsgmap = &s->rbmsgmap;

	if (msg == NULL) {
		return -1;
	}

	nni_msg *tmsg = NULL;
	tmsg = nni_id_get(rbmsgmap, key);
	if (tmsg == NULL) {
		return -1;
	}

	*msg = tmsg;
	return 0;
}

int
exchange_client_get_msgs_by_key(void *arg, uint64_t key, uint32_t count, nng_msg ***list)
{
	int ret = 0;
	nni_msg *tmsg = NULL;
	exchange_sock_t *s = arg;

	nni_id_map *rbmsgmap = &s->rbmsgmap;
	uint32_t key2 = key & 0XFFFFFFFF;
	tmsg = nni_id_get(rbmsgmap, key2);
	if (tmsg == NULL || list == NULL) {
		log_error("tmsg is NULL or list is NULL\n");
		return -1;
	}

	if (count == 1) {
		nng_msg **newList = nng_alloc(sizeof(nng_msg *));
		if (newList == NULL) {
			log_error("Failed to allocate memory for newList\n");
			return -1;
		}

		newList[0] = tmsg;
		*list = newList;
	} else {
		/* Only one exchange with one ringBuffer now */
		ret = ringBuffer_search_msgs_by_key(s->ex_node->ex->rbs[0], key, count, list);
		if (ret != 0 || *list == NULL) {
			log_error("ringBuffer_get_msgs_by_key failed!\n");
			return -1;
		}
	}

	return 0;
}

int
exchange_client_get_msgs_fuzz(void *arg, uint64_t start, uint64_t end, uint32_t *count, nng_msg ***list)
{
	int ret = 0;
	exchange_sock_t *s = arg;

	/* Only one exchange with one ringBuffer now */
	ret = ringBuffer_search_msgs_fuzz(s->ex_node->ex->rbs[0], start, end, count, list);
	if (ret != 0 || *list == NULL) {
		return -1;
	}

	return 0;
}

void
decToHex(unsigned char decimal, char *hexadecimal)
{
	unsigned char remainder = 0;
	if (decimal > 0) {
		remainder = decimal % 16;
		if (remainder < 10) {
			hexadecimal[1] = remainder + '0';
		} else {
			hexadecimal[1] = remainder + 'a' - 10;
		}

		decimal /= 16;
		remainder = decimal % 16;
		if (remainder < 10) {
			hexadecimal[0] = remainder + '0';
		} else {
			hexadecimal[0] = remainder + 'a' - 10;
		}
	}

	return;
}

#if defined(SUPP_BLF)
static char *
get_file_bname(char *fpath)
{
	char *bname;
#ifdef _WIN32
	if ((bname = nng_alloc(strlen(fpath) + 16)) == NULL) return NULL;
	char ext[16];
	_splitpath_s(fpath,
		NULL, 0,    // Don't need drive
		NULL, 0,    // Don't need directory
		bname, strlen(fpath) + 15,  // just the filename
		ext, 15);
	strncpy(bname + strlen(bname), ext, 15);
#else
	#include <libgen.h>
	bname = basename(fpath);
#endif
	return bname;
}

static int
get_persistence_files_raw(uint32_t sz, char **fnames, char ***file_raws)
{

	int ret = 0;
	long file_size = 0L;
	FILE *fp = NULL;
	char *payload = NULL;

	if (file_raws == NULL || fnames == NULL || sz == 0) {
		return -1;
	}

	*file_raws = nng_alloc(sz * sizeof(char *));
	if (*file_raws == NULL) {
		log_warn("Failed to allocate memory for file payload\n");
		return -1;
	}

	for (uint32_t i = 0; i < sz; i++) {
		fp = fopen(fnames[i], "rb");
		if (fp == NULL) {
			log_warn("Failed to open file\n");
			nng_free(*file_raws, sz * sizeof(char *));
			return -1;
		}
		fseek(fp, 0L, SEEK_END);
		file_size = ftell(fp);
		rewind(fp);

		payload = (char *)nng_alloc(file_size);
		if (payload == NULL) {
			log_warn("Failed to allocate memory for file payload\n");
			nng_free(*file_raws, sz * sizeof(char *));
			return -1;
		}

		ret = fread(payload, 1, file_size, fp);
		if (ret <= 0) {
			log_warn("Failed to read file\n");
			nng_free(payload, file_size);
			return -1;
		}
		fclose(fp);

		(*file_raws)[i] = nng_alloc(file_size * 2 + 1 + 1);
		if ((*file_raws)[i] == NULL) {
			log_warn("Failed to allocate memory for file payload\n");
			nng_free(payload, file_size);
			return -1;
		}
		(*file_raws)[i][file_size * 2 + 1] = '\0';

		for (int j = 0; j < file_size; ++j) {
			char hex[2];
			hex[0] = '0';
			hex[1] = '0';

			decToHex(payload[j] & 0xff, hex);

			(*file_raws)[i][j * 2] = hex[0];
			(*file_raws)[i][j * 2 + 1] = hex[1];
		}
		nng_free(payload, file_size);
	}

	return 0;
}

#if defined(SUPP_BLF)
static int
get_persistence_files(uint32_t sz, char **fnames, cJSON *obj)
{
	int ret;

	log_info("Ask MQ persistence and found.");
	const char ** filenames = nng_alloc(sizeof(char *) * sz);
	for (uint32_t i = 0; i < sz; ++i) {
		filenames[i] = get_file_bname((char *)fnames[i]);
	}

	cJSON *files_obj = cJSON_CreateStringArray((const char * const *)fnames, sz);
	cJSON_AddItemToObject(obj, "files", files_obj);
	if (!files_obj) {
		return -1;
	}

	cJSON *filenames_obj = cJSON_CreateStringArray(filenames, sz);
	if (!filenames_obj) {
		return -1;
	}
	cJSON_AddItemToObject(obj, "filenames", filenames_obj);

	char **file_raws = NULL;
	ret = get_persistence_files_raw(sz, fnames, &file_raws);
	if (ret != 0) {
		return -1;
	}

	cJSON *fileraws_obj = cJSON_CreateStringArray((const char * const *)file_raws, sz);
	if (!fileraws_obj) {
		return -1;
	}
	cJSON_AddItemToObject(obj, "fileraws", fileraws_obj);

	nng_free(filenames, sizeof(char *) * sz);

	for (int i = 0; i<(int)sz; ++i) {
		nng_free((void *)fnames[i], 0);
		nng_free(file_raws[i], 0);
	}

	nng_free(file_raws, sizeof(char *) * sz);
	nng_free(fnames, sz);

	return 0;
}
#endif
#endif

#ifdef SUPP_PARQUET
static int
dump_file_result_cat(char **msgList, int *msgLen, uint32_t count, cJSON *obj)
{
	uint32_t sz = 0;
	uint32_t pos = 0;

	if (msgList == NULL || msgLen == NULL || count == 0) {
		return -1;
	}

	for (uint32_t i = 0; i < count; i++) {
		sz += msgLen[i];
	}

	char **mqdata = nng_alloc(sizeof(char *) * count);
	memset(mqdata, 0, sizeof(char *) * count);
	for (uint32_t i = 0; i < count; i++) {
		if (sz >= pos + msgLen[i]) {
			mqdata[i] = nng_alloc(msgLen[i] * 2 + 1);
			memset(mqdata[i], '\0', msgLen[i] * 2 + 1);
			if (mqdata[i] == NULL) {
				log_warn("Failed to allocate memory for file payload\n");
				return -1;
			}
			for (int j = 0; j < msgLen[i]; ++j) {
				char *tmpch = (char *)msgList[i];
				char hex[2];
				hex[0] = '0';
				hex[1] = '0';

				decToHex(tmpch[j] & 0xff, hex);

				mqdata[i][j * 2] = hex[0];
				mqdata[i][j * 2 + 1] = hex[1];
			}
		} else {
			log_error("buffer overflow!");
			return -1;
		}
		pos += msgLen[i];
	}

	cJSON *files_obj = cJSON_CreateStringArray((const char * const*)mqdata, count);
	if (!files_obj) {
		return -1;
	}
	cJSON_AddItemToObject(obj, "file", files_obj);

	for (uint32_t i = 0; i < count; ++i) {
		nng_free(mqdata[i], 0);
	}
	nng_free(mqdata, sizeof(char *) * count);

	return 0;
}
#endif

#ifdef SUPP_PARQUET
static inline int find_keys_in_file(ringBuffer_t *rb, uint64_t *keys, uint32_t count, cJSON *obj)
{
	int ret = 0;
	int msgCount = 0;
	int *msgLen = NULL;
	char **msgs = NULL;

	/* Only one exchange with one ringBuffer now */
	msgCount = ringBuffer_get_msgs_from_file_by_keys(rb, keys, count, (void ***)&msgs, &msgLen);
	if (msgCount <= 0 || msgs == NULL || msgLen == NULL) {
		log_error("not found msgs in file!");
	} else {
		ret = dump_file_result_cat(msgs, msgLen, msgCount, obj);

		for(int i = 0; i < msgCount; ++i) {
			nng_free(msgs[i], 0);
		}
		nng_free(msgs, sizeof(char *) * msgCount);
		nng_free(msgLen, sizeof(int) * msgCount);

		if (ret != 0) {
			log_error("dump_file_result_cat failed!");
			return -1;
		}
	}

	return 0;
}
#endif

static int
exchange_pipe_init(void *arg, nni_pipe *pipe, void *s)
{
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(pipe);
	NNI_ARG_UNUSED(s);
	return (0);
}

static int
exchange_pipe_start(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return 0;
//	exchange_pipe_t *p = arg;
//	exchange_sock_t *s = p->sock;

	// might be useful in MQ switching
	// if (nni_pipe_peer(p->pipe) != NNI_PROTO_EXCHANGE_V0) {
	// 	// Peer protocol mismatch.
	// 	return (NNG_EPROTO);
	// }
	// int rv = nni_id_set(&s->pipes, nni_pipe_id(p->pipe), p);
	return (0);
}

static void
exchange_pipe_stop(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return;
}

static int
exchange_pipe_close(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return (0);
}

static void
exchange_pipe_fini(void *arg)
{
	NNI_ARG_UNUSED(arg);
	return;
}

static nni_proto_pipe_ops exchange_pipe_ops = {
	.pipe_size  = sizeof(exchange_pipe_t),
	.pipe_init  = exchange_pipe_init,
	.pipe_fini  = exchange_pipe_fini,
	.pipe_start = exchange_pipe_start,
	.pipe_close = exchange_pipe_close,
	.pipe_stop  = exchange_pipe_stop,
};

static nni_proto_ctx_ops exchange_ctx_ops = {
	.ctx_size    = 0,
	.ctx_init    = NULL,
	.ctx_fini    = NULL,
	.ctx_recv    = NULL,
	.ctx_send    = NULL,
	.ctx_options = NULL,
};

static nni_option exchange_sock_options[] = {
	{
	    .o_name = NNG_OPT_EXCHANGE_BIND,
	    .o_set  = exchange_sock_bind_exchange,
	},
	{
		.o_name = NNG_OPT_EXCHANGE_GET_RBMSGMAP,
		.o_get  = exchange_sock_get_rbmsgmap,
	},
	{
		.o_name = NNG_OPT_EXCHANGE_START_LIMIT_TIMER,
		.o_set  = exchange_sock_start_limit_timer,
	},
	{
	    .o_name = NULL,
	},
};

static nni_proto_sock_ops exchange_sock_ops = {
	.sock_size    = sizeof(exchange_sock_t),
	.sock_init    = exchange_sock_init,
	.sock_fini    = exchange_sock_fini,
	.sock_open    = exchange_sock_open,
	.sock_close   = exchange_sock_close,
	.sock_options = exchange_sock_options,
	.sock_send    = exchange_sock_send,
	.sock_recv    = exchange_sock_recv,
};

static nni_proto exchange_proto = {
	.proto_version  = NNI_PROTOCOL_VERSION,
	// necessary for compatbility with req of NNG-SP 
	.proto_self     = { NNG_REP0_SELF, NNG_REP0_SELF_NAME },
	.proto_peer     = { NNG_REP0_PEER, NNG_REP0_PEER_NAME },
	.proto_flags    = NNI_PROTO_FLAG_SNDRCV,
	.proto_sock_ops = &exchange_sock_ops,
	.proto_pipe_ops = &exchange_pipe_ops,
	.proto_ctx_ops  = &exchange_ctx_ops,
};

// init pair0_sock inside of exchange_sock, only allowed being called once at init
static void
exchange_sock_setsock(void *arg, void *data)
{
	exchange_sock_t *s = arg;
	s->pair0_sock      = data;
	// to start first recv
	nng_recv_aio(*s->pair0_sock, &s->query_aio);
	log_info("start to recv\n");
}

int
nng_exchange_client_open(nng_socket *sock)
{
	return (nni_proto_mqtt_open(sock, &exchange_proto, exchange_sock_setsock));
}
