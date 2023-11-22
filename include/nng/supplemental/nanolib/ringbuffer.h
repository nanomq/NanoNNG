#include <stdio.h>
#include <stdlib.h>

#define RINGBUFFER_MAX_SIZE	0xffff
#define RBRULELIST_MAX_SIZE	0xff

#define ENQUEUE_IN_HOOK     0x0001
#define ENQUEUE_OUT_HOOK    0x0010
#define DEQUEUE_IN_HOOK     0x0100
#define DEQUEUE_OUT_HOOK    0x1000
#define HOOK_MASK           ((ENQUEUE_IN_HOOK) | (ENQUEUE_OUT_HOOK) | (DEQUEUE_IN_HOOK) | (DEQUEUE_OUT_HOOK))

struct ringBufferMsg {
	void *data;
	/* TTL of each message */
	unsigned long long expiredAt;
};

struct ringBuffer {
	unsigned int       head;
	unsigned int       tail;
	unsigned int       size;
	unsigned int       cap;
	/* Whether to allow overwriting of old data when the queue is full */
	unsigned int       overWrite;
	/* TTL of all messages in ringbuffer */
	unsigned long long expiredAt;

	/* TODO: LOCK */

	struct ringBufferMsg *msgs;
};

#define RINGBUFFER_MAX_SIZE 0xffff

int ringBuffer_init(struct ringBuffer **rb,
					unsigned int cap,
					unsigned int overWrite,
					unsigned long long expiredAt);
int ringBuffer_enqueue(struct ringBuffer *rb,
					   void *data,
					   unsigned long long expiredAt);
int ringBuffer_dequeue(struct ringBuffer *rb, void **data);
int ringBuffer_release(struct ringBuffer *rb);
