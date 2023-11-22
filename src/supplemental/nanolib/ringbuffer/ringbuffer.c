#include "nng/supplemental/nanolib/ringbuffer.h"
#include "core/nng_impl.h"

int ringBuffer_init(struct ringBuffer **rb,
					unsigned int cap,
					unsigned int overWrite,
					unsigned long long expiredAt)
{
	struct ringBuffer *newRB;

	if (cap >= RINGBUFFER_MAX_SIZE) {
		log_error("Want to init a ring buffer which is greater than MAX_SIZE: %u\n", RINGBUFFER_MAX_SIZE);
		return -1;
	}

	newRB = (struct ringBuffer *)nng_alloc(sizeof(struct ringBuffer));
	if (newRB == NULL) {
		log_error("New ring buffer alloc failed\n");
		return -1;
	}

	newRB->msgs = (struct ringBufferMsg *)nng_alloc(sizeof(struct ringBufferMsg) * cap);
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
	newRB->overWrite = overWrite;

	newRB->enqinRuleListLen = 0;
	newRB->enqoutRuleListLen = 0;
	newRB->deqinRuleListLen = 0;
	newRB->deqoutRuleListLen = 0;

	*rb = newRB;

	return 0;
}

static inline int ringBufferRule_check(struct ringBuffer *rb,
									   struct ringBufferRule **list,
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

static inline int ringBuffer_rule_check(struct ringBuffer *rb, void *data, int flag)
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

int ringBuffer_enqueue(struct ringBuffer *rb,
					   void *data,
					   unsigned long long expiredAt)
{
	int ret;
	ret = ringBuffer_rule_check(rb, data, ENQUEUE_IN_HOOK);
	if (ret != 0) {
		return -1;
	}

	if (rb->size == rb->cap) {
		if (rb->overWrite) {
			nng_free(rb->msgs[rb->head].data, sizeof(*rb->msgs[rb->head].data));
			rb->msgs[rb->head].data = data;
			rb->msgs[rb->head].expiredAt = expiredAt;
			rb->head = (rb->head + 1) % rb->cap;
			rb->tail = (rb->tail + 1) % rb->cap;
			log_error("Ring buffer is full but overwrite the old data\n");
			return 0;
		} else {
			log_error("Ring buffer is full enqueue failed!!!\n");
			return -1;
		}
	}

	struct ringBufferMsg *msg = &rb->msgs[rb->tail];

	msg->data = data;
	msg->expiredAt = expiredAt;

	rb->tail = (rb->tail + 1) % rb->cap;
	rb->size++;

	(void)ringBuffer_rule_check(rb, data, ENQUEUE_OUT_HOOK);

	return 0;
}

int ringBuffer_dequeue(struct ringBuffer *rb, void **data)
{
	int ret;
	ret = ringBuffer_rule_check(rb, NULL, DEQUEUE_IN_HOOK);
	if (ret != 0) {
		return -1;
	}

	if (rb->size == 0) {
		log_error("Ring buffer is NULL dequeue failed\n");
		return -1;
	}

	*data = rb->msgs[rb->head].data;
	rb->head = (rb->head + 1) % rb->cap;
	rb->size = rb->size - 1;

	(void)ringBuffer_rule_check(rb, *data, DEQUEUE_OUT_HOOK);

	return 0;
}

int ringBuffer_release(struct ringBuffer *rb)
{
	unsigned int i = 0;
	unsigned int count = 0;

	if (rb == NULL) {
		return -1;
	}

	if (rb->msgs != NULL) {
		if (rb->size != 0) {
			i = rb->head;
			count = 0;
			while (count < rb->size) {
				nng_free(rb->msgs[i].data, sizeof(*(rb->msgs[i].data)));
				i = (i + 1) % rb->cap;
				count++;
			}
		}
		nng_free(rb->msgs, sizeof(*rb->msgs));
	}
	nng_free(rb, sizeof(*rb));

	return 0;
}

static inline int ringBufferRuleList_add(struct ringBufferRule **list, unsigned int *len,
										 int (*match)(struct ringBuffer *rb, void *data, int flag),
										 int (*target)(struct ringBuffer *rb, void *data, int flag))
{
	struct ringBufferRule *newRule = NULL;
	if (*len == RBRULELIST_MAX_SIZE) {
		log_error("Rule Buffer enqueue rule list is full!\n");
		return -1;
	}

	newRule = nng_alloc(sizeof(struct ringBufferRule));
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

int ringBuffer_add_rule(struct ringBuffer *rb,
						int (*match)(struct ringBuffer *rb, void *data, int flag),
						int (*target)(struct ringBuffer *rb, void *data, int flag),
						int flag)
{
	int ret;

	if (rb == NULL || match == NULL || target == NULL || (flag & HOOK_MASK) == 0) {
		return -1;
	}

	if (flag & ENQUEUE_IN_HOOK) {
		ret = ringBufferRuleList_add(rb->enqinRuleList, &rb->enqinRuleListLen, match, target);
		if (ret != 0) {
			return -1;
		}
	}

	if (flag & ENQUEUE_OUT_HOOK) {
		ret = ringBufferRuleList_add(rb->enqoutRuleList, &rb->enqoutRuleListLen, match, target);
		if (ret != 0) {
			return -1;
		}
	}

	if (flag & DEQUEUE_IN_HOOK) {
		ret = ringBufferRuleList_add(rb->deqinRuleList, &rb->deqinRuleListLen, match, target);
		if (ret != 0) {
			return -1;
		}
	}

	if (flag & DEQUEUE_OUT_HOOK) {
		ret = ringBufferRuleList_add(rb->deqoutRuleList, &rb->deqoutRuleListLen, match, target);
		if (ret != 0) {
			return -1;
		}
	}

	return 0;
}
