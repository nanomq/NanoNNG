//
// Copyright 2023 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <stdio.h>
#include <stdlib.h>
#include "nng/nng.h"
#include "nng/supplemental/util/platform.h"

#define RBNAME_LEN          100
#define RINGBUFFER_MAX_SIZE	0xffff
#define RBRULELIST_MAX_SIZE	0xff

#define ENQUEUE_IN_HOOK     0x0001
#define ENQUEUE_OUT_HOOK    0x0010
#define DEQUEUE_IN_HOOK     0x0100
#define DEQUEUE_OUT_HOOK    0x1000
#define HOOK_MASK           ((ENQUEUE_IN_HOOK) | (ENQUEUE_OUT_HOOK) | (DEQUEUE_IN_HOOK) | (DEQUEUE_OUT_HOOK))

typedef struct ringBuffer_s ringBuffer_t;
typedef struct ringBufferMsg_s ringBufferMsg_t;
typedef struct ringBufferRule_s ringBufferRule_t;
typedef struct ringBuffer_msgs_s ringBuffer_msgs_t;

struct ringBuffer_msgs_s {
	int key;
	nng_msg *msg;
};

struct ringBufferMsg_s {
	int  key;
	void *data;
	/* TTL of each message */
	unsigned long long expiredAt;
};

struct ringBuffer_s {
	char                    name[RBNAME_LEN];
	unsigned int            head;
	unsigned int            tail;
	unsigned int            size;
	unsigned int            cap;
	/* Whether to allow overwriting of old data when the queue is full */
	unsigned int            overWrite;
	/* TTL of all messages in ringbuffer */
	unsigned long long      expiredAt;
	unsigned int            enqinRuleListLen;
	unsigned int            enqoutRuleListLen;
	unsigned int            deqinRuleListLen;
	unsigned int            deqoutRuleListLen;
	ringBufferRule_t        *enqinRuleList[RBRULELIST_MAX_SIZE];
	ringBufferRule_t        *enqoutRuleList[RBRULELIST_MAX_SIZE];
	ringBufferRule_t        *deqinRuleList[RBRULELIST_MAX_SIZE];
	ringBufferRule_t        *deqoutRuleList[RBRULELIST_MAX_SIZE];

	nng_mtx                 *ring_lock;

	ringBufferMsg_t *msgs;
};

struct ringBufferRule_s {
	/*
	 * flag: ENQUEUE_IN_HOOK/ENQUEUE_OUT_HOOK/DEQUEUE_IN_HOOK/DEQUEUE_OUT_HOOK
	 * return: 0: success, -1: failed
	 */
	int (*match)(ringBuffer_t *rb, void *data, int flag);
	/*
	 * return: 0: continue, -1: stop and return
	 */
	int (*target)(ringBuffer_t *rb, void *data, int flag);
};


int ringBuffer_add_rule(ringBuffer_t *rb,
						int (*match)(ringBuffer_t *rb, void *data, int flag),
						int (*target)(ringBuffer_t *rb, void *data, int flag),
						int flag);

int ringBuffer_init(ringBuffer_t **rb,
					unsigned int cap,
					unsigned int overWrite,
					unsigned long long expiredAt);
int ringBuffer_enqueue(ringBuffer_t *rb,
					   int key,
					   void *data,
					   unsigned long long expiredAt);
int ringBuffer_dequeue(ringBuffer_t *rb, void **data);
int ringBuffer_release(ringBuffer_t *rb);

int ringBuffer_search_msg_by_key(ringBuffer_t *rb, int key, nng_msg **msg);
int ringBuffer_search_msgs_by_key(ringBuffer_t *rb, uint32_t key, int count, void ***list);
#endif
