#include "core/nng_impl.h"
#include "nng/exchange/exchange.h"

int
exchange_init(exchange_t **ex, char *name, char *topic, uint8_t streamType, uint32_t chunk_size,
			  unsigned int *rbsCaps, char **rbsName, uint8_t *rbsFullOp, unsigned int rbsCount)
{
	int ret = 0;
	exchange_t *newEx = NULL;

	if (ex == NULL) {
		return -1;
	}
	if (rbsCount >= RINGBUFFER_MAX) {
		return -1;
	}

	if (name == NULL || strlen(name) > EXCHANGE_NAME_LEN) {
		log_error("Exchange init failed, name invalid!\n");
		return -1;
	}

	if (rbsName == NULL) {
		return -1;
	}

	for (unsigned int i = 0; i < rbsCount; i++) {
		if (rbsName[i] == NULL || strlen(rbsName[i]) >= RBNAME_LEN) {
			return -1;
		}
	}

	if (rbsCaps == NULL) {
		return -1;
	}
	for (unsigned int i = 0; i < rbsCount; i++) {
		if (rbsCaps[i] >= RINGBUFFER_MAX_SIZE) {
			return -1;
		}
	}

	newEx = (exchange_t *)nng_alloc(sizeof(exchange_t));
	if (newEx == NULL) {
		log_error("Exchange init failed, alloc memory failed!\n");
		return -1;
	}

	memset(newEx->name, 0, EXCHANGE_NAME_LEN);
	memset(newEx->topic, 0, TOPIC_NAME_LEN);
	(void)strcpy(newEx->name, name);
	(void)strcpy(newEx->topic, topic);

	newEx->streamType = streamType;
	newEx->chunk_size = chunk_size;
	newEx->streamAio = NULL;

	for (unsigned int i = 0; i < RINGBUFFER_MAX; i++) {
		newEx->rbs[i] = NULL;
	}

	newEx->rb_count = 0;

	for (unsigned int i = 0; i < rbsCount; i++) {
		ringBuffer_t *rb = NULL;
		ret = ringBuffer_init(&rb, rbsCaps[i], rbsFullOp[i], -1);
		if (rb == NULL || ret != 0) {
			for (unsigned int j = 0; j < newEx->rb_count; j++) {
				ringBuffer_release(newEx->rbs[j]);
			}
			nng_free(newEx, sizeof(*newEx));
			return -1;
		}
		(void)strcpy(rb->name, rbsName[i]);
		newEx->rbs[i] = rb;
		newEx->rb_count++;
	}

	*ex = newEx;

	return 0;
}

int
exchange_add_rb(exchange_t *ex, ringBuffer_t *rb)
{
	if (ex == NULL || rb == NULL) {
		return -1;
	}

	if (ex->rb_count == RINGBUFFER_MAX) {
		log_error("Exchange add ringBuffer failed, ringBuffer list is full!\n");
		return -1;
	}

	ex->rbs[ex->rb_count++] = rb;

	return 0;
}

int
exchange_release(exchange_t *ex)
{
	unsigned int i;

	if (ex == NULL) {
		return -1;
	}

	for (i = 0; i < ex->rb_count; i++) {
		(void)ringBuffer_release(ex->rbs[i]);
	}

	nng_free(ex, sizeof(*ex));

	return 0;
}

int
exchange_handle_msg(exchange_t *ex, uint64_t key, void *msg, nng_aio *aio)
{
	unsigned int i = 0;
	int ret = 0;

	if (ex == NULL || msg == NULL) {
		return -1;
	}

	for (i = 0; i < ex->rb_count; i++) {
		log_debug("handling msg key %ld", key);
		ret = ringBuffer_enqueue(ex->rbs[i], key, msg, -1, aio);
		if (ret != 0) {
			log_error("Ring Buffer enqueue failed\n");
			return -1;
		} else {
			log_debug("msg enqueued! msg %p key: %d", msg, key);
		}
	}

	return 0;
}

int
exchange_get_ringBuffer(exchange_t *ex, char *rbName, ringBuffer_t **rb)
{
	for (unsigned int i = 0; i < ex->rb_count; i++) {
		if (strcmp(ex->rbs[i]->name, rbName) == 0) {
			*rb = ex->rbs[i];
			return 0;
		}
	}

	return -1;
}
