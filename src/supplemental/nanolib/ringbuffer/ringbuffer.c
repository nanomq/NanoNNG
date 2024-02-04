//
// Copyright 2023 NanoMQ Team, Inc.
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/supplemental/nanolib/ringbuffer.h"
#include "core/nng_impl.h"

static inline int ringBuffer_get_msgs(ringBuffer_t *rb, unsigned int *count, nng_msg ***list)
{
	unsigned int i = 0;
	unsigned int j = 0;

	nng_msg **newList = NULL;

	if (*count == 0 || *count > rb->size) {
		newList = nng_alloc(rb->size * sizeof(nng_msg *));
		*count = rb->size;
	} else {
		newList = nng_alloc((*count) * sizeof(nng_msg *));
	}
	if (newList == NULL) {
		return -1;
	}

	for (i = rb->head; i < rb->size; i++) {
		i = i % rb->cap;
		nng_msg *msg = rb->msgs[i].data;
		nng_msg_set_proto_data(msg, NULL, (void *)(uintptr_t)rb->msgs[i].key);

		newList[j] = msg;

		j++;
		if (j == *count) {
			*list = newList;
			return 0;
		}
	}

	nng_free(newList, sizeof(*newList));
	return -1;
}

static inline void ringBuffer_clean_msgs(ringBuffer_t *rb, int needFree)
{
	unsigned int i = 0;
	unsigned int count = 0;

	if (rb->msgs != NULL) {
		if (rb->size != 0) {
			i = rb->head;
			count = 0;
			while (count < rb->size) {
				if (rb->msgs[i].data != NULL) {
					if (needFree) {
						/* For nni_msg now */
						nng_msg_free(rb->msgs[i].data);
					}
					rb->msgs[i].data = NULL;
				}

				i = (i + 1) % rb->cap;
				count++;
			}
		}
	}

	rb->head = 0;
	rb->tail = 0;
	rb->size = 0;

	return;
}

int ringBuffer_get_and_clean_msgs(ringBuffer_t *rb,
								  unsigned int *count, nng_msg ***list)
{
	int ret;

	if (rb == NULL || list == NULL || count == NULL) {
		return -1;
	}
	if (rb->size == 0) {
		*list = NULL;
		*count = 0;
		log_error("Ring buffer is empty!\n");
		return -1;
	}

	ret = ringBuffer_get_msgs(rb, count, list);
	if (ret != 0) {
		return -1;
	}

	ringBuffer_clean_msgs(rb, 0);

	return 0;
}

int ringBuffer_init(ringBuffer_t **rb,
					unsigned int cap,
					enum fullOption fullOp,
					unsigned long long expiredAt)
{
	ringBuffer_t *newRB;

	if (cap >= RINGBUFFER_MAX_SIZE) {
		log_error("Want to init a ring buffer which is greater than MAX_SIZE: %u\n", RINGBUFFER_MAX_SIZE);
		return -1;
	}

	if (fullOp >= RB_FULL_MAX) {
		log_error("fullOp is not valid: %d\n", fullOp);
		return -1;
	}

	newRB = (ringBuffer_t *)nng_alloc(sizeof(ringBuffer_t));
	if (newRB == NULL) {
		log_error("New ring buffer alloc failed\n");
		return -1;
	}

	newRB->msgs = (ringBufferMsg_t *)nng_alloc(sizeof(ringBufferMsg_t) * cap);
	if (newRB->msgs == NULL) {
		log_error("New ringbuffer messages alloc failed\n");
		nng_free(newRB, sizeof(*newRB));
		return -1;
	}

	newRB->head = 0;
	newRB->tail = 0;
	newRB->size = 0;
	newRB->cap = cap;

	newRB->expiredAt = expiredAt;
	newRB->fullOp = fullOp;
	newRB->files = NULL;

	newRB->enqinRuleList[0] = NULL;
	newRB->enqoutRuleList[0] = NULL;
	newRB->deqinRuleList[0] = NULL;
	newRB->deqoutRuleList[0] = NULL;

	newRB->enqinRuleListLen = 0;
	newRB->enqoutRuleListLen = 0;
	newRB->deqinRuleListLen = 0;
	newRB->deqoutRuleListLen = 0;

	nng_mtx_alloc(&newRB->ring_lock);

	*rb = newRB;

	return 0;
}

static inline int ringBufferRule_check(ringBuffer_t *rb,
									   ringBufferRule_t **list,
									   unsigned int len,
									   void *data,
									   int flag)
{
	int ret = 0;
	unsigned int i = 0;

	for (i = 0; i < len; i++) {
		ret = list[i]->match(rb, data, flag);
		if (ret != 0) {
			continue;
		}
		ret = list[i]->target(rb, data, flag);
		if (ret != 0) {
			return -1;
		}
	}

	return 0;
}

static inline int ringBuffer_rule_check(ringBuffer_t *rb, void *data, int flag)
{
	int ret;

	if (flag & ENQUEUE_IN_HOOK) {
		ret = ringBufferRule_check(rb, rb->enqinRuleList, rb->enqinRuleListLen, data, ENQUEUE_IN_HOOK);
		if (ret != 0) {
			return -1;
		}
	}
	if (flag & ENQUEUE_OUT_HOOK) {
		ret = ringBufferRule_check(rb, rb->enqoutRuleList, rb->enqoutRuleListLen, data, ENQUEUE_OUT_HOOK);
		if (ret != 0) {
			return -1;
		}
	}
	if (flag & DEQUEUE_IN_HOOK) {
		ret = ringBufferRule_check(rb, rb->deqinRuleList, rb->deqinRuleListLen, data, DEQUEUE_IN_HOOK);
		if (ret != 0) {
			return -1;
		}
	}
	if (flag & DEQUEUE_OUT_HOOK) {
		ret = ringBufferRule_check(rb, rb->deqoutRuleList, rb->deqoutRuleListLen, data, DEQUEUE_OUT_HOOK);
		if (ret != 0) {
			return -1;
		}
	}

	return 0;
}

#ifdef SUPP_PARQUET
void ringbuffer_parquet_cb(void *arg)
{
	ringBufferFile_t *file = (ringBufferFile_t *)arg;
	if (file == NULL) {
		log_error("parquet callback arg is NULL\n");
		return;
	}
	if (nng_aio_result(file->aio) != 0) {
		log_error("parquet write file failed\n");
		return;
	}

	nng_msg **smsgs = (nng_msg **)nng_aio_get_prov_data(file->aio);
	if (smsgs == NULL) {
		log_error("parquet callback smsgs is NULL\n");
		return;
	}

	parquet_file_ranges *file_ranges = (parquet_file_ranges *)nng_aio_get_output(file->aio, 1);
	if (file_ranges == NULL) {
		log_error("parquet file range is NULL\n");
		return;
	}

	uint32_t *szp = (uint32_t *)nng_aio_get_msg(file->aio);
	if (szp == NULL) {
		log_error("parquet file size is NULL\n");
		return;
	}

	int count = 0;
	for (int i = file_ranges->start; count < file_ranges->size; count++, i++) {
		if (i >= file_ranges->size) {
			i = 0;
		}
		parquet_file_range **file_range = file_ranges->range;

		ringBufferFileRange_t *range = nng_alloc(sizeof(ringBufferFileRange_t));
		if (range == NULL) {
			log_error("alloc new file range failed! no memory! msg will be freed\n");
			return;
		}

		range->startidx = file_range[i]->start_idx;
		range->endidx = file_range[i]->end_idx;
		range->filename = nng_alloc(sizeof(char) * strlen(file_range[i]->filename) + 1);
		if (range->filename == NULL) {
			log_error("alloc new file range filename failed! no memory! msg will be freed\n");
			nng_free(range, sizeof(ringBufferFileRange_t));
			range = NULL;
			return;
		}
		range->filename[strlen(file_range[i]->filename)] = '\0';

		(void)strncpy(range->filename, file_range[i]->filename, strlen(file_range[i]->filename));

		cvector_push_back(file->ranges, range);
		log_warn("ringbus: parquet write to file: %s success\n", file_range[i]->filename);
	}
	for (uint32_t i = 0; i < *szp; i++) {
		if (smsgs[i] != NULL) {
			nng_msg_free(smsgs[i]);
			smsgs[i] = NULL;
		}
	}

	if (smsgs != NULL) {
		nng_free(smsgs, sizeof(nng_msg *) * *szp);
		smsgs = NULL;
	}

	return;
}

static parquet_object *init_parquet_object(ringBuffer_t *rb, ringBufferFile_t *file)
{
	if (rb == NULL || file == NULL) {
		log_error("parquet object or ringbuffer is NULL\n");
		return NULL;
	}

	uint8_t **darray = nng_alloc(sizeof(uint8_t *) * rb->size);
	uint32_t *dsize = nng_alloc(sizeof(uint32_t) * rb->size);
	uint64_t *keys = nng_alloc(sizeof(uint64_t) * rb->size);

	if (keys == NULL || darray == NULL || dsize == NULL) {
		log_error("alloc new keys darray dsize failed! no memory! msg will be freed\n");

		if (keys != NULL) {
			nng_free(keys, sizeof(uint64_t) * rb->size);
		}
		if (darray != NULL) {
			nng_free(darray, sizeof(uint8_t *) * rb->size);
		}
		if (dsize != NULL) {
			nng_free(dsize, sizeof(uint32_t) * rb->size);
		}

		return NULL;
	}

	nng_msg **smsgs = nng_alloc(sizeof(nng_msg *) * rb->size);
	for (unsigned int i = 0; i < rb->size; i++) {
		keys[i] = rb->msgs[i].key;
		smsgs[i] = rb->msgs[i].data;
		darray[i] = nng_msg_payload_ptr((nng_msg *)rb->msgs[i].data);
		dsize[i] = nng_msg_len((nng_msg *)rb->msgs[i].data) -
		        (nng_msg_payload_ptr((nng_msg *)rb->msgs[i].data) -
				(uint8_t *)nng_msg_body((nng_msg *)rb->msgs[i].data));
	}

	nng_aio *aio;
	nng_aio_alloc(&aio, ringbuffer_parquet_cb, file);
	if(aio == NULL) {
		log_error("alloc new aio failed! no memory! msg will be freed\n");
		nng_free(keys, sizeof(uint64_t) * rb->size);
		nng_free(darray, sizeof(uint8_t *) * rb->size);
		nng_free(dsize, sizeof(uint32_t) * rb->size);
		return NULL;
	}

	file->aio = aio;
	file->ranges = NULL;

	nng_aio_begin(aio);

	parquet_object *newObj = parquet_object_alloc(keys, darray, dsize, rb->size, aio, smsgs);
	if (newObj == NULL) {
		log_error("alloc new parquet object failed! no memory! msg will be freed\n");
		nng_free(keys, sizeof(uint64_t) * rb->size);
		nng_free(darray, sizeof(uint8_t *) * rb->size);
		nng_free(dsize, sizeof(uint32_t) * rb->size);
		return NULL;
	}

	return newObj;
}

static inline void free_msgs_from_file(uint64_t *keys, char **filenames,
									   char **newList, int *newmsgLen,
									   parquet_data_packet **packet,
									   uint32_t packet_count, uint32_t request_count)
{

	if (keys != NULL) {
		nng_free(keys, sizeof(uint64_t) * request_count);
	}

	if (filenames != NULL) {
		nng_free(filenames, sizeof(char *) * request_count);
	}

	if (newList != NULL) {
		for (uint32_t i = 0; i < packet_count; i++) {
			if (newList[i] != NULL) {
				nng_free(newList[i], sizeof(*newList[i]));
				newList[i] = NULL;
			}
		}
		nng_free(newList, sizeof(char *) * packet_count);
	}

	if (newmsgLen != NULL) {
		nng_free(newmsgLen, sizeof(int) * request_count);
	}

	if (packet != NULL) {
		for (uint32_t i = 0; i < request_count; i++) {
			if (packet[i] != NULL) {
				nng_free(packet[i]->data, packet[i]->size);
				nng_free(packet[i], sizeof(parquet_data_packet));
			}
		}

		nng_free(packet, sizeof(parquet_data_packet *) * request_count);
	}
}

static inline int ringBuffer_get_filenames_with_keys(ringBuffer_t *rb, char ***filenames, uint64_t *keys, uint32_t count)
{
	int file_count = 0;
	if (rb == NULL || rb->files == NULL || filenames == NULL || keys == NULL) {
		log_error("ringbuffer is NULL or files is NULL or filenames is NULL or keys is NULL\n");
		return -1;
	}
	char **fnames = nng_alloc(sizeof(char *) * count);
	if (fnames == NULL) {
		log_error("alloc new fnames failed! no memory! msg will be freed\n");
		return -1;
	}

	for (uint32_t i = 0; i < count; i++) {
		fnames[i] = NULL;
		for (uint32_t j = 0; j < cvector_size(rb->files); j++) {
			ringBufferFile_t *file = rb->files[j];
			if (file == NULL || file->keys == NULL) {
				log_error("file is NULL or file keys is NULL\n");
				nng_free(fnames, sizeof(char *) * count);
				return -1;
			}

			for (uint32_t k = 0; k < rb->cap; k++) {
				if (file->keys[k] == keys[i]) {
					fnames[i] = file->ranges[0]->filename;
					file_count++;
					break;
				}
			}
		}
	}

	*filenames = fnames;

	return file_count;
}

static inline int init_newList_with_packet(void ***newList, int **newmsgLen,
										   parquet_data_packet **packet,
										   uint32_t packet_count, uint32_t request_count)
{
	char **list = nng_alloc(sizeof(char *) * packet_count);
	if (list == NULL) {
		log_error("alloc new list failed! no memory! msg will be freed\n");
		return -1;
	}

	int *msgLen = nng_alloc(sizeof(int) * packet_count);
	if (msgLen == NULL) {
		log_error("alloc new msgLen failed! no memory! msg will be freed\n");
		nng_free(list, sizeof(char *) * packet_count);
		return -1;
	}

	int msgidx = 0;
	for (long unsigned int i = 0; i < request_count; i++) {
		if(packet[i] == NULL) {
			continue;
		}

		char *msg = nng_alloc(packet[i]->size);
		if (msg == NULL) {
			log_error("alloc new msg failed! no memory! msg will be freed\n");
			nng_free(list, sizeof(char *) * packet_count);
			nng_free(msgLen, sizeof(int) * packet_count);
			return -1;
		}

		memcpy(msg, packet[i]->data, packet[i]->size);

		msgLen[msgidx] = packet[i]->size;
		list[msgidx++] = msg;
	}

	*newList = (void **)list;
	*newmsgLen = msgLen;

	return 0;
}

int ringBuffer_get_msgs_from_file_by_keys(ringBuffer_t *rb, uint64_t *keys, uint32_t count,
										  void ***msgs, int **msgLen)
{
	if (rb == NULL || rb->files == NULL || keys == NULL || msgs == NULL || msgLen == NULL) {
		log_error("ringbuffer is NULL or files is NULL or keys is NULL or msg is NULL or msgLen is NULL\n");
		return -1;
	}

	char **filenames = NULL;
	int ret = ringBuffer_get_filenames_with_keys(rb, &filenames, keys, count);
	if (ret <= 0 || filenames == NULL) {
		log_error("get filenames failed\n");
		return -1;
	}

	parquet_data_packet **packet = parquet_find_data_packets(NULL, filenames, keys, count);
	if (packet == NULL) {
		log_error("packet is NULL\n");
		nng_free(filenames, sizeof(char *) * count);

		return -1;
	}

	int packet_count = 0;
	for (long unsigned int i = 0; i < count; i++) {
		if (packet[i] != NULL) {
			packet_count++;
		}
		/* todo: Cleaning up or update outdated messages */
	}

	if (packet_count == 0) {
		log_error("packet count is 0\n");
		nng_free(filenames, sizeof(char *) * count);

		return -1;
	}

	ret = init_newList_with_packet(msgs, msgLen, packet, packet_count, count);
	if (ret != 0) {
		log_error("init new list with packet failed\n");
		free_msgs_from_file(NULL, filenames, NULL, NULL,
							packet, packet_count, count);

		return -1;
	}

	free_msgs_from_file(NULL, filenames, NULL, NULL,
						packet, packet_count, count);

	return packet_count;

}

int ringBuffer_get_msgs_from_file(ringBuffer_t *rb, void ***msgs, int **msgLen)
{
	int ret = 0;

	if (rb == NULL || rb->files == NULL || msgs == NULL || msgLen == NULL) {
		log_error("ringbuffer is NULL or files is NULL or msgs is NULL or msgLen is NULL\n");
		return -1;
	}

	int count = 0;
	for (int i = 0; i < cvector_size(rb->files); i++) {
		for (int j = 0; j < cvector_size(rb->files[i]->ranges); j++) {
			count += rb->files[i]->ranges[j]->endidx - rb->files[i]->ranges[j]->startidx + 1;
		}
	}

	uint64_t *keys = nng_alloc(sizeof(uint64_t) * count);
	if (keys == NULL) {
		log_error("alloc new keys failed! no memory! msg will be freed\n");
		return -1;
	}

	char **filenames = nng_alloc(sizeof(char *) * count);
	if (filenames == NULL) {
		log_error("alloc new filenames failed! no memory! msg will be freed\n");
		free_msgs_from_file(keys, NULL, NULL, NULL,
							NULL, 0, count);
		return -1;
	}

	int tmpidx = 0;
	for(long unsigned int i = 0; i < cvector_size(rb->files); i++) {
		ringBufferFile_t *file = rb->files[i];
		if (file == NULL || file->ranges == NULL) {
			log_error("file is NULL or file ranges is NULL\n");
			free_msgs_from_file(keys, filenames, NULL, NULL,
								NULL, 0, count);
			return -1;
		}

		for (int j = 0; j < cvector_size(file->ranges); j++) {
			for (unsigned int k = file->ranges[j]->startidx; k <= file->ranges[j]->endidx; k++) {
				keys[tmpidx] = file->keys[k];
				filenames[tmpidx++] = file->ranges[j]->filename;
			}
		}
	}

	int packet_count = 0;
	parquet_data_packet **packet = parquet_find_data_packets(NULL, filenames, keys, count);
	if (packet == NULL) {
		log_error("packet is NULL\n");
		free_msgs_from_file(keys, filenames, NULL, NULL,
							NULL, 0, count);

		return -1;
	}

	for (long unsigned int i = 0; i < count; i++) {
		if (packet[i] != NULL) {
			packet_count++;
		}
		/* todo: Cleaning up or update outdated messages */
	}

	if (packet_count == 0) {
		log_error("packet count is 0\n");
		free_msgs_from_file(keys, filenames, NULL, NULL,
							packet, packet_count, count);

		return -1;
	}

	ret = init_newList_with_packet(msgs, msgLen, packet, packet_count, count);
	if (ret != 0) {
		log_error("init new list with packet failed\n");
		free_msgs_from_file(keys, filenames, NULL, NULL,
							packet, packet_count, count);

		return -1;
	}

	free_msgs_from_file(keys, filenames, NULL, NULL,
						packet, packet_count, count);

	return packet_count;
}
#endif

static int write_msgs_to_file(ringBuffer_t *rb)
{
#ifdef SUPP_PARQUET
	ringBufferFile_t *file = nng_alloc(sizeof(ringBufferFile_t));
	if (file == NULL) {
		log_error("alloc new file failed! no memory! msg will be freed\n");
		ringBuffer_clean_msgs(rb, 1);
		return -1;
	}

	parquet_object *obj = init_parquet_object(rb, file);
	if (obj == NULL) {
		log_error("init parquet object failed! msg will be freed\n");
		ringBuffer_clean_msgs(rb, 1);
		nng_free(file, sizeof(ringBufferFile_t));
		return -1;
	}

	(void)parquet_write_batch_async(obj);

	file->keys = nng_alloc(sizeof(uint64_t) * rb->cap);
	if (file->keys == NULL) {
		log_error("alloc new file keys failed! no memory! msg will be freed\n");
		ringBuffer_clean_msgs(rb, 1);
		nng_free(file, sizeof(ringBufferFile_t));
		return -1;
	}

	for (unsigned int i = 0; i < rb->cap; i++) {
		file->keys[i] = rb->msgs[i].key;
	}

	cvector_push_back(rb->files, file);

	/* free msgs in callback */
	ringBuffer_clean_msgs(rb, 0);

	return 0;
#else
	log_error("parquet is not enable, msg will be freed\n");
	ringBuffer_clean_msgs(rb, 1);
	return -1;
#endif
}

static int put_msgs_to_aio(ringBuffer_t *rb, nng_aio *aio)
{
	int ret = 0;
	int *list_len = NULL;

	/* get all msgs and clean ringbuffer */
	nni_msg **list = NULL;
	ret = ringBuffer_get_and_clean_msgs(rb, &rb->cap, &list);
	if (ret != 0 || list == NULL) {
		log_error("Ring buffer is full and clean ringbuffer failed!\n");
		return -1;
	}

	/* Put list len in msg proto data */
	list_len = nng_alloc(sizeof(int));
	if (list_len == NULL) {
		log_error("alloc new list_len failed! no memory!\n");
		return -1;
	}
	*list_len = rb->cap;

	nng_msg *tmsg;
	ret = nng_msg_alloc(&tmsg, 0);
	if (ret != 0 || tmsg == NULL) {
		log_error("alloc new msg failed! no memory!\n");
		return -1;
	}

	// just use aio count : nni_aio_count(nni_aio *aio)
	nng_msg_set_proto_data(tmsg, NULL, (void *)list_len);
	nng_aio_set_msg(aio, tmsg);
	nng_aio_set_prov_data(aio, (void *)list);

	return 0;
}

int ringBuffer_enqueue(ringBuffer_t *rb,
					   uint64_t key,
					   void *data,
					   unsigned long long expiredAt,
					   nng_aio *aio)
{
	int ret;

	nng_mtx_lock(rb->ring_lock);
	ret = ringBuffer_rule_check(rb, data, ENQUEUE_IN_HOOK);
	if (ret != 0) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	if (rb->size == rb->cap) {
		if (rb->fullOp == RB_FULL_NONE) {
			log_error("Ring buffer is full enqueue failed!!!\n");
			nng_mtx_unlock(rb->ring_lock);
			return -1;
		}
		if (rb->fullOp == RB_FULL_DROP) {
			ringBuffer_clean_msgs(rb, 1);
		}
		if (rb->fullOp == RB_FULL_RETURN) {
			ret = put_msgs_to_aio(rb, aio);
			if (ret != 0) {
				log_error("Ring buffer is full and put msgs to aio failed!\n");
				nng_mtx_unlock(rb->ring_lock);
				return -1;
			}
		}
		if (rb->fullOp == RB_FULL_FILE) {
			ret = write_msgs_to_file(rb);
			if (ret != 0) {
				log_error("Ring buffer is full and write msgs to file failed!\n");
				nng_mtx_unlock(rb->ring_lock);
				return -1;
			}
		}
	}

	ringBufferMsg_t *msg = &rb->msgs[rb->tail];

	msg->key = key;
	msg->data = data;
	msg->expiredAt = expiredAt;

	rb->tail = (rb->tail + 1) % rb->cap;
	rb->size++;

	(void)ringBuffer_rule_check(rb, data, ENQUEUE_OUT_HOOK);

	nng_mtx_unlock(rb->ring_lock);
	return 0;
}

int ringBuffer_dequeue(ringBuffer_t *rb, void **data)
{
	int ret;
	nng_mtx_lock(rb->ring_lock);
	ret = ringBuffer_rule_check(rb, NULL, DEQUEUE_IN_HOOK);
	if (ret != 0) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	if (rb->size == 0) {
		log_error("Ring buffer is NULL dequeue failed\n");
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	*data = rb->msgs[rb->head].data;
	rb->head = (rb->head + 1) % rb->cap;
	rb->size = rb->size - 1;

	(void)ringBuffer_rule_check(rb, *data, DEQUEUE_OUT_HOOK);

	nng_mtx_unlock(rb->ring_lock);
	return 0;
}

static inline void ringBufferRuleList_release(ringBufferRule_t **list, unsigned int len)
{
	unsigned int i = 0;

	if (list == NULL) {
		return;
	}

	for (i = 0; i < len; i++) {
		if (list[i] != NULL) {
			nng_free(list[i], sizeof(sizeof(ringBufferRule_t)));
			list[i] = NULL;
		}
	}

	return;
}

int ringBuffer_release(ringBuffer_t *rb)
{
	unsigned int i = 0;
	unsigned int count = 0;

	if (rb == NULL) {
		return -1;
	}

	nng_mtx_lock(rb->ring_lock);
	if (rb->msgs != NULL) {
		if (rb->size != 0) {
			i = rb->head;
			count = 0;
			while (count < rb->size) {
				if (rb->msgs[i].data != NULL) {
					/* For nni_msg now */
					nng_msg_free(rb->msgs[i].data);
					rb->msgs[i].data = NULL;
				}
				i = (i + 1) % rb->cap;
				count++;
			}
		}
		nng_free(rb->msgs, sizeof(*rb->msgs));
	}

	if (rb->files != NULL) {
		for (int i = 0; i < (int)cvector_size(rb->files); i++) {
			nng_free(rb->files[i]->keys, sizeof(uint64_t) * rb->cap);
			nng_free(rb->files[i], sizeof(ringBufferFile_t));
		}
		cvector_free(rb->files);
	}

	ringBufferRuleList_release(rb->enqinRuleList, rb->enqinRuleListLen);
	ringBufferRuleList_release(rb->deqinRuleList, rb->deqinRuleListLen);
	ringBufferRuleList_release(rb->enqoutRuleList, rb->enqoutRuleListLen);
	ringBufferRuleList_release(rb->deqoutRuleList, rb->deqoutRuleListLen);

	nng_mtx_unlock(rb->ring_lock);
	nng_mtx_free(rb->ring_lock);
	nng_free(rb, sizeof(*rb));

	return 0;
}

static inline int ringBufferRuleList_add(ringBufferRule_t **list, unsigned int *len,
										 int (*match)(ringBuffer_t *rb, void *data, int flag),
										 int (*target)(ringBuffer_t *rb, void *data, int flag))
{
	ringBufferRule_t *newRule = NULL;
	if (*len == RBRULELIST_MAX_SIZE) {
		log_error("Rule Buffer enqueue rule list is full!\n");
		return -1;
	}

	newRule = nng_alloc(sizeof(ringBufferRule_t));
	if (newRule == NULL) {
		log_error("alloc new rule failed! no memory!\n");
		return -1;
	}

	newRule->match = match;
	newRule->target = target;
	list[*len] = newRule;
	*len = *len + 1;
	return 0;
}

int ringBuffer_add_rule(ringBuffer_t *rb,
						int (*match)(ringBuffer_t *rb, void *data, int flag),
						int (*target)(ringBuffer_t *rb, void *data, int flag),
						int flag)
{
	int ret;

	if (rb == NULL || match == NULL || target == NULL || (flag & HOOK_MASK) == 0) {
		return -1;
	}

	nng_mtx_lock(rb->ring_lock);
	if (flag & ENQUEUE_IN_HOOK) {
		ret = ringBufferRuleList_add(rb->enqinRuleList, &rb->enqinRuleListLen, match, target);
		if (ret != 0) {
			nng_mtx_unlock(rb->ring_lock);
			return -1;
		}
	}

	if (flag & ENQUEUE_OUT_HOOK) {
		ret = ringBufferRuleList_add(rb->enqoutRuleList, &rb->enqoutRuleListLen, match, target);
		if (ret != 0) {
			nng_mtx_unlock(rb->ring_lock);
			return -1;
		}
	}

	if (flag & DEQUEUE_IN_HOOK) {
		ret = ringBufferRuleList_add(rb->deqinRuleList, &rb->deqinRuleListLen, match, target);
		if (ret != 0) {
			nng_mtx_unlock(rb->ring_lock);
			return -1;
		}
	}

	if (flag & DEQUEUE_OUT_HOOK) {
		ret = ringBufferRuleList_add(rb->deqoutRuleList, &rb->deqoutRuleListLen, match, target);
		if (ret != 0) {
			nng_mtx_unlock(rb->ring_lock);
			return -1;
		}
	}

	nng_mtx_unlock(rb->ring_lock);
	return 0;
}

int ringBuffer_search_msg_by_key(ringBuffer_t *rb, uint64_t key, nng_msg **msg)
{
	unsigned int i = 0;

	if (rb == NULL || msg == NULL) {
		return -1;
	}

	nng_mtx_lock(rb->ring_lock);
	for (i = rb->head; i < rb->size; i++) {
		i = i % rb->cap;
		if (rb->msgs[i].key == key) {
			*msg = rb->msgs[i].data;
			nng_mtx_unlock(rb->ring_lock);
			return 0;
		}
	}

	nng_mtx_unlock(rb->ring_lock);
	return -1;
}

/*
 * binary search, only for ringbuffer head is 0
 */
int ringBuffer_search_msgs_fuzz(ringBuffer_t *rb,
								uint64_t start,
								uint64_t end,
								uint32_t *count,
								nng_msg ***list)
{
	uint32_t low = 0;
	uint32_t high = 0;
	uint32_t mid = 0;
	uint32_t start_index = 0;
	uint32_t end_index = 0;

	nng_mtx_lock(rb->ring_lock);
	if (rb == NULL || rb->size == 0 || count == NULL || list == NULL) {
		nng_mtx_unlock(rb->ring_lock);
		log_error("ringbuffer is NULL or count is NULL or list is NULL\n");
		return -1;
	}

	if (start > end ||
		start > rb->msgs[rb->head + rb->size - 1].key ||
		end < rb->msgs[rb->head].key) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	if (rb->head != 0) {
		log_error("Ringbuffer head is not 0, can not use binary search\n");
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	low	= rb->head;
	high = rb->head + rb->size - 1;
	while (low < high) {
		mid = (low + high) / 2;
		if (rb->msgs[mid].key < start) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}

	if (rb->msgs[high].key >= start) {
		start_index = high;
	} else {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	low	= rb->head;
	high = rb->head + rb->size - 1;
	while (low < high) {
		mid = (low + high + 1) / 2;
		if (rb->msgs[mid].key <= end) {
			low = mid;
		} else {
			high = mid - 1;
		}
	}

	if (rb->msgs[low].key > end) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	} else {
		end_index = low;
	}

	*count = end_index - start_index + 1;
	nng_msg **newList = nng_alloc((*count) * sizeof(nng_msg *));
	if (newList == NULL) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	for (uint32_t i = start_index; i <= end_index; i++) {
		nng_msg *msg = rb->msgs[i].data;
		if (msg == NULL) {
			nng_free(newList, sizeof(*newList));
			nng_mtx_unlock(rb->ring_lock);
			log_error("msg is NULL and some error occured\n");
			return -1;
		}
		nng_msg_set_proto_data(msg, NULL, (void *)(uintptr_t)rb->msgs[i].key);
		newList[i - start_index] = msg;
	}

	*list = newList;
	nng_mtx_unlock(rb->ring_lock);
	return 0;
}

int ringBuffer_search_msgs_by_key(ringBuffer_t *rb, uint64_t key, uint32_t count, nng_msg ***list)
{
	unsigned int i = 0;
	unsigned int j = 0;

	if (rb == NULL || count <= 0 || list == NULL) {
		return -1;
	}

	nng_mtx_lock(rb->ring_lock);
	if (count > rb->size) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	nng_msg **newList = nng_alloc(count * sizeof(nng_msg *));
	if (newList == NULL) {
		nng_mtx_unlock(rb->ring_lock);
		return -1;
	}

	for (i = rb->head; i < rb->size; i++) {
		i = i % rb->cap;
		if (rb->msgs[i].key == key) {
			for (j = 0; j < count; j++) {
				nng_msg *msg = rb->msgs[i].data;

				nng_msg_set_proto_data(msg, NULL, (void *)(uintptr_t)rb->msgs[i].key);

				newList[j] = msg;

				i = (i + 1) % rb->cap;
			}
			*list = newList;
			nng_mtx_unlock(rb->ring_lock);
			return 0;
		}
	}

	nng_mtx_unlock(rb->ring_lock);
	nng_free(newList, sizeof(*newList));
	return -1;
}
