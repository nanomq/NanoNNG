#include "nng/exchange/exchange.h"
#include "core/nng_impl.h"

int
exchange_init(exchange_t **ex, char *name)
{
	exchange_t *newEx = NULL;

	if (ex == NULL) {
		return -1;
	}

	if (name == NULL || strlen(name) > EXCHANGE_NAME_LEN) {
		log_error("Exchange init failed, name invalid!\n");
		return -1;
	}

	newEx = (exchange_t *)nng_alloc(sizeof(exchange_t));
	if (newEx == NULL) {
		log_error("Exchange init failed, alloc memory failed!\n");
		return -1;
	}

	memset(newEx->name, 0, EXCHANGE_NAME_LEN);
	(void)strcpy(newEx->name, name);

	for (int i = 0; i < PRODUCER_MAX; i++) {
		newEx->prods[i] = NULL;
	}

	newEx->prods_count = 0;

	*ex = newEx;

	return 0;
}

int
exchange_add_prod(exchange_t *ex, producer_t *prod)
{
	if (ex == NULL || prod == NULL) {
		return -1;
	}

	if (ex->prods_count == PRODUCER_MAX) {
		log_error("Exchange add producer failed, producer list is full!\n");
		return -1;
	}

	ex->prods[ex->prods_count++] = prod;

	return 0;
}

int
exchange_release(exchange_t *ex)
{
	unsigned int i;

	if (ex == NULL) {
		return -1;
	}

	for (i = 0; i < ex->prods_count; i++) {
		(void)producer_release(ex->prods[i]);
	}

	nng_free(ex, sizeof(*ex));

	return 0;
}

int
exchange_handle_msg(exchange_t *ex, void *msg)
{
	unsigned int i = 0;

	if (ex == NULL || msg == NULL) {
		return -1;
	}

	for (i = 0; i < ex->prods_count; i++) {
		if (ex->prods[i]->match(msg) == 0) {
			if (ex->prods[i]->target(msg) != 0) {
				/* stop and return */
				return -1;
			}
		}
	}

	return 0;
}
