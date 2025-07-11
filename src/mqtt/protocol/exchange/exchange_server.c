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
#include "nng/exchange/exchange_client.h"
#include "nng/exchange/exchange.h"
#include "supplemental/mqtt/mqtt_msg.h"
#include "nng/protocol/reqrep0/rep.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/blf.h"

#define NANO_MAX_MQ_BUFFER_LEN 1024

#ifndef NNI_PROTO_EXCHANGE_V0
#define NNI_PROTO_EXCHANGE_V0 NNI_PROTO(15, 0)
#endif

typedef struct exchange_sock_s         exchange_sock_t;
typedef struct exchange_node_s         exchange_node_t;
typedef struct exchange_pipe_s         exchange_pipe_t;

// one MQ, one Sock(TBD), one PIPE
struct exchange_pipe_s {
	nni_pipe        *pipe;
	exchange_sock_t *sock;
	bool             closed;
	uint32_t         id;
	nni_aio          ex_aio;	// recv cmd from Consumer
	nni_aio          rp_aio;	// send msg to consumer
	nni_lmq          lmq;
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
	exchange_pipe_t *pipe;
	bool            isBusy;
};

struct exchange_sock_s {
	nni_mtx         mtx;
	nni_atomic_bool closed;
	nni_id_map      rbmsgmap;
	nni_id_map      pipes;		//pipe = consumer client
	exchange_node_t *ex_node;
	nni_pollable    readable;
	nni_pollable    writable;
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

	node->isBusy = false;
	node->ex = ex;
	node->sock = s;

	s->ex_node = node;
	nni_mtx_unlock(&s->mtx);
	return 0;
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
	nni_id_map_init(&s->pipes, 0, 0, false);

	nni_pollable_init(&s->writable);
	nni_pollable_init(&s->readable);
	return;
}

static void
exchange_sock_fini(void *arg)
{
	exchange_sock_t *s = arg;
	exchange_node_t *ex_node;

	ex_node = s->ex_node;

	nni_pollable_fini(&s->writable);
	nni_pollable_fini(&s->readable);
	exchange_release(ex_node->ex);

	nni_free(ex_node, sizeof(*ex_node));
	ex_node = NULL;

	nni_id_map_fini(&s->pipes);
	nni_id_map_fini(&s->rbmsgmap);
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
		log_error("msg already in rbmsgmap, overwirte is not allowed");
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = nni_id_set(&ex_node->sock->rbmsgmap, key, msg);
	if (ret != 0) {
		log_error("rbmsgmap set failed");
		/* free msg here! */
		nni_msg_free(msg);
		return -1;
	}

	ret = exchange_handle_msg(ex_node->ex, key, msg, aio);
	if (ret != 0) {
		log_error("exchange_handle_msg failed!\n");
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
		log_error("exchange sock is closed!");
		nni_aio_finish_error(user_aio, NNG_EINVAL);
		return;
	}

	ret = exchange_client_handle_msg(ex_node, msg, user_aio);
	if (ret != 0) {
		log_error(
		    "exchange_client_handle cached msg failed!\n");
		nni_aio_finish_error(user_aio, NNG_EINVAL);
	} else {
		nng_msg *tmsg = nng_aio_get_msg(user_aio);
		if (tmsg != NULL) {
			char *topic = ex_node->ex->topic;
			nng_msg_set_conn_param(tmsg, topic);
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
	if (msg == NULL) {
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}
	if (nni_msg_get_type(msg) != CMD_PUBLISH) {
		nni_aio_finish_error(aio, NNG_EINVAL);
		return;
	}
	nni_mtx_lock(&s->mtx);
	if (s->ex_node == NULL) {
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
	for (int i=0; i<(int)node->rbufs_sz; ++i) {
		cvector_push_back(rbsName, node->rbufs[i]->name);
		cvector_push_back(rbsCap, node->rbufs[i]->cap);
		cvector_push_back(rbsFullOp, node->rbufs[i]->fullOp);
	}

	exchange_t *ex = NULL;
	rv = exchange_init(&ex, node->name, node->topic,
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

int
exchange_client_get_msg_by_key(void *arg, uint64_t key, nni_msg **msg)
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
		log_error("ringBuffer_get_msgs_fuzz failed!\n");
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

#if defined(SUPP_PARQUET) || defined(SUPP_BLF)
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

static int fuzz_search_result_cat(nng_msg **msgList,
								  uint32_t count,
								  unsigned char **pringbusdata,
								  int *pringbusdata_len)
{
	uint32_t sz = 0;
	uint32_t pos = 0;
	uint32_t diff = 0;

	if (msgList == NULL || count == 0) {
		return -1;
	}

	for (uint32_t i = 0; i < count; i++) {
		diff = nng_msg_len(msgList[i]) -
			((uintptr_t)nng_msg_payload_ptr(msgList[i]) - (uintptr_t)nng_msg_body(msgList[i]));
		sz += diff;
	}

	unsigned char *ringbusdata = nng_alloc(sz);
	int index = 0;
	for (uint32_t i = 0; i < count; i++) {
		diff = nng_msg_len(msgList[i]) -
			((uintptr_t)nng_msg_payload_ptr(msgList[i]) - (uintptr_t)nng_msg_body(msgList[i]));
		if (sz >= pos + diff) {
			char *tmpch = (char *)nng_msg_payload_ptr(msgList[i]);
			for (int j = 0; j < diff; j++) {
				ringbusdata[index++] = tmpch[j];
			}
		} else {
			log_error("buffer overflow!");
			return -1;
		}
		pos += diff;
	}

	*pringbusdata = ringbusdata;
	*pringbusdata_len = sz;

	return 0;
}

static inline int splitstr(char *str, char *delim, char *result[], int max_num)
{
	char *p;
	int count = 0;
	p = strtok(str, delim);
	while (p != NULL && count < max_num) {
		result[count++] = p;
		p = strtok(NULL, delim);
	}
	return count;
}

#if defined(SUPP_PARQUET)
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

/**
 * For exchanger, recv_cb is a consumer SDK
 * TCP/QUIC/IPC/InPROC is at your disposal
*/
static void
ex_query_recv_cb(void *arg)
{
	int ret = 0;
	exchange_pipe_t *p = arg;
	exchange_sock_t *sock = p->sock;
	nni_msg *msg = NULL;
	uint8_t *body = NULL;
	char *parquetdata = NULL;
	unsigned char *ringbusdata = NULL;
	int parquetdata_len = 0;
	int ringbusdata_len = 0;

	if (nni_aio_result(&p->ex_aio) != 0) {
		nni_pipe_close(p->pipe);
		return;
	}

	msg = nni_aio_get_msg(&p->ex_aio);
	nni_aio_set_msg(&p->ex_aio, NULL);
	nni_msg_set_pipe(msg, nni_pipe_id(p->pipe));

	body = nni_msg_body(msg);

	nni_mtx_lock(&sock->mtx);

	// process query
	char *keystr = (char *) (body + 4);
	if (keystr == NULL) {
		log_error("error in paring keystr");
		nni_mtx_unlock(&sock->mtx);
		return;
	}

	/* fuzz search */
	uint64_t startKey = 0;
	uint64_t endKey = 0;
	uint32_t count = 0;
	nng_msg **msgList = NULL;

	log_info("Recv command: %s", keystr);

	ret = sscanf(keystr, "%"SCNu64"-%"SCNu64, &startKey, &endKey);
	if (ret == 0) {
		log_error("error in read key to number %s", keystr);
		nni_mtx_unlock(&sock->mtx);
		return;
	}
	log_info("Start fuzz search startKey: %"PRIu64", endKey: %"PRIu64, startKey, endKey);

	ret = exchange_client_get_msgs_fuzz(sock, startKey, endKey, &count, &msgList);
	if (ret == 0 && count != 0 && msgList != NULL) {
		ret = fuzz_search_result_cat(msgList, count, &ringbusdata, &ringbusdata_len);
		if (ret != 0) {
			log_error("fuzz_search_result_cat failed!");
		}
		nng_free(msgList, sizeof(nng_msg *) * count);
	} else {
		log_error("exchange_client_get_msgs_fuzz failed! count: %d", count);
	}

#if defined(SUPP_PARQUET)
	int size = 0;
	parquet_data_packet **parquet_objs = NULL;
	parquet_objs = parquet_find_data_span_packets(NULL, startKey, endKey, &size, sock->ex_node->ex->topic);
	if (parquet_objs != NULL && size > 0) {
		int total_len = 0;
		for (int i = 0; i < size; i++) {
			total_len += parquet_objs[i]->size;
		}
		parquetdata = nng_alloc(total_len);
		if (parquetdata == NULL) {
			log_error("Failed to allocate memory for file payload\n");
			nni_mtx_unlock(&sock->mtx);
			return;
		}
		for (int i = 0; i < size; i++) {
			memcpy(parquetdata + parquetdata_len, parquet_objs[i]->data, parquet_objs[i]->size);
			parquetdata_len += parquet_objs[i]->size;
		}
		for (int i = 0; i < size; i++) {
			nng_free(parquet_objs[i]->data, parquet_objs[i]->size);
			nng_free(parquet_objs[i], sizeof(parquet_data_packet));
		}
		nng_free(parquet_objs, sizeof(parquet_data_packet *) * size);
	} else {
		log_error("parquet_find_data_span_packets failed! size: %d", size);
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
	log_info("found parquetdata_len: %d, ringbusdata_len: %d", parquetdata_len, ringbusdata_len);

	nni_msg_chop(msg, strlen(keystr));
	if (parquetdata != NULL && parquetdata_len > 0) {
		nni_msg_append(msg, parquetdata, parquetdata_len);
		nng_free(parquetdata, parquetdata_len);
		parquetdata = NULL;
	}
	if (ringbusdata != NULL && ringbusdata_len > 0) {
		nni_msg_append(msg, ringbusdata, ringbusdata_len);
		nng_free(ringbusdata, ringbusdata_len);
		ringbusdata = NULL;
	}

	nni_aio_wait(&p->rp_aio);
	nni_time time = 3000;
	nni_aio_set_expire(&p->rp_aio, time);
	nni_aio_set_msg(&p->rp_aio, msg);
	nni_pipe_send(p->pipe, &p->rp_aio);
	nni_mtx_unlock(&sock->mtx);
	nni_pipe_recv(p->pipe, &p->ex_aio);

	return;
}

static void
ex_query_send_cb(void *arg)
{
	exchange_pipe_t       *p    = arg;
	exchange_sock_t       *sock = p->sock;

	NNI_ARG_UNUSED(sock);
}

static int
exchange_pipe_init(void *arg, nni_pipe *pipe, void *s)
{
	exchange_pipe_t *p = arg;

	nni_aio_init(&p->ex_aio, ex_query_recv_cb, p);
	nni_aio_init(&p->rp_aio, ex_query_send_cb, p);
	nni_lmq_init(&p->lmq, 256);

	p->pipe = pipe;
	p->id   = nni_pipe_id(pipe);
	p->sock = s;
	return (0);
}

static int
exchange_pipe_start(void *arg)
{
	exchange_pipe_t *p = arg;
	exchange_sock_t *s = p->sock;

	// might be useful in MQ switching
	// if (nni_pipe_peer(p->pipe) != NNI_PROTO_EXCHANGE_V0) {
	// 	// Peer protocol mismatch.
	// 	return (NNG_EPROTO);
	// }

	nni_mtx_lock(&s->mtx);
	int rv = nni_id_set(&s->pipes, nni_pipe_id(p->pipe), p);
	nni_mtx_unlock(&s->mtx);
	if (rv != 0) {
		return (rv);
	}
	nni_pipe_recv(p->pipe, &p->ex_aio);
	return (0);
}

static void
exchange_pipe_stop(void *arg)
{
	exchange_pipe_t *p = arg;

	nni_aio_stop(&p->ex_aio);
	nni_aio_stop(&p->rp_aio);
	return;
}

static int
exchange_pipe_close(void *arg)
{
	exchange_pipe_t *p = arg;
	exchange_sock_t *s = p->sock;

	nni_aio_close(&p->ex_aio);
	nni_aio_close(&p->rp_aio);

	nni_mtx_lock(&s->mtx);
	p->closed = true;

	nni_id_remove(&s->pipes, nni_pipe_id(p->pipe));
	nni_mtx_unlock(&s->mtx);

	return (0);
}

static void
exchange_pipe_fini(void *arg)
{
	exchange_pipe_t *p = arg;

	nng_msg *  msg;

	if ((msg = nni_aio_get_msg(&p->ex_aio)) != NULL) {
		nni_aio_set_msg(&p->ex_aio, NULL);
		nni_msg_free(msg);
	}

	nni_aio_fini(&p->ex_aio);
	nni_aio_fini(&p->rp_aio);
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

int
nng_exchange_client_open(nng_socket *sock)
{
	return (nni_proto_open(sock, &exchange_proto));
}
