//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "iceoryx_api.h"

#include "nng/nng.h"
#include "core/nng_impl.h"

#include "iceoryx_binding_c/listener.h"
#include "iceoryx_binding_c/runtime.h"
#include "iceoryx_binding_c/subscriber.h"
#include "iceoryx_binding_c/publisher.h"
#include "iceoryx_binding_c/types.h"
#include "iceoryx_binding_c/user_trigger.h"

nni_id_map *suber_map = NULL;

struct nano_iceoryx_suber {
	iox_listener_t listener;
	iox_sub_t      suber;
	nni_lmq       *recvmq;
};

struct nano_iceoryx_puber {
	iox_pub_t      puber;
};

// Event is the topic you wanna read
int
nano_iceoryx_init(const char *const name)
{
    iox_runtime_init(name); // No related to subscriber or publisher. just a runtime name
	
	if ((suber_map = nng_alloc(sizeof(*suber_map))) == NULL)
		return NNG_ENOMEM;
	nni_id_map_init(suber_map, 0, 0xffffffff, false);

	return 0;
}

int
nano_iceoryx_fini()
{
	iox_runtime_shutdown();
	nni_id_map_fini(suber_map);
	return 0;
}

void
nano_iceoryx_listener_alloc(nano_iceoryx_listener **listenerp)
{
    iox_listener_storage_t listener_storage;
    iox_listener_t listener = iox_listener_init(&listener_storage);

	*listenerp = listener;
}

void
nano_iceoryx_listener_free(nano_iceoryx_listener *listener)
{
    iox_listener_deinit((iox_listener_t)listener);
}

static void
suber_recv_cb(iox_sub_t subscriber)
{
	NNI_ARG_UNUSED(subscriber);
}

nano_iceoryx_suber *
nano_iceoryx_suber_alloc(const char *subername, const char *const service_name,
    const char *const instance_name, const char *const event,
    nano_iceoryx_listener *lstner)
{
	iox_listener_t listener = lstner;
	nano_iceoryx_suber *suber = nng_alloc(sizeof(*suber));
	if (!suber)
		return NULL;

    iox_sub_options_t options;
    iox_sub_options_init(&options);
    options.historyRequest = 10U;
    options.queueCapacity = 50U;
    options.nodeName = subername;

    iox_sub_storage_t subscriber_storage;
    iox_sub_t subscriber = iox_sub_init(&subscriber_storage, service_name,
	    instance_name, event, &options);

    iox_listener_attach_subscriber_event(
        (iox_listener_t)listener, subscriber, SubscriberEvent_DATA_RECEIVED, &suber_recv_cb);

	nni_lmq_init(suber->recvmq, NANO_ICEORYX_RECVQ_LEN);

	int rv;
	if (0 != (rv = nni_id_set(suber_map, (uint64_t)subscriber, suber))) {
		log_error("Failed to set suber_map %d", rv);
		nano_iceoryx_suber_free(suber);
		return NULL;
	}

	suber->listener = listener;
	suber->suber    = subscriber;

	return suber;
}

void
nano_iceoryx_suber_free(nano_iceoryx_suber *suber)
{
    iox_listener_detach_subscriber_event(suber->listener, suber->suber,
	        SubscriberEvent_DATA_RECEIVED);
    iox_sub_deinit(suber->suber);
	nni_lmq_fini(suber->recvmq);
	nng_free(suber, sizeof(*suber));
}

nano_iceoryx_puber *
nano_iceoryx_puber_alloc(const char *pubername, const char *const service_name,
    const char *const instance_name, const char *const event)
{
	nano_iceoryx_puber *puber = nng_alloc(sizeof(*puber));
	if (!puber)
		return NULL;

    iox_pub_options_t options;
    iox_pub_options_init(&options);
    options.historyCapacity = 10U;
    options.nodeName = pubername;

    iox_pub_storage_t publisher_storage;

    iox_pub_t publisher = iox_pub_init(&publisher_storage, service_name,
		instance_name, event, &options);

	puber->puber = publisher;
	return puber;
}

void
nano_iceoryx_puber_free(nano_iceoryx_puber *puber)
{
	nng_free(puber, sizeof(*puber));
}

int
nano_iceoryx_msg_alloc_raw(void **msgp, size_t sz, nano_iceoryx_puber *puber)
{
	// Not a common result code. So +8 when try to find the real reason code.
	return iox_pub_loan_chunk(puber->puber, msgp, sz) - 8;
}

// U32 SZ | U32 ID | PAYLOAD
int
nano_iceoryx_msg_alloc(void **msgp, nano_iceoryx_puber *puber, uint32_t id, nng_msg *msg)
{
	int rv;
	size_t sz;
	uint8_t *m;

	sz = nng_msg_len(msg);
	sz += (NANO_ICEORYX_SZ_BYTES + NANO_ICEORYX_ID_BYTES);

	if (0 != (rv = nano_iceoryx_msg_alloc_raw(&m, sz, puber))) {
		log_error("FAiled to alloc iceoryx chunk %d", rv);
		return rv;
	}
	*msgp = m;

	NNI_PUT32(m, sz);
	m += NANO_ICEORYX_SZ_BYTES;
	NNI_PUT32(m, id);
	m += NANO_ICEORYX_ID_BYTES;
	memcpy(m, nng_msg_body(msg), nng_msg_len(msg));

	return 0;
}

void
nano_iceoryx_write_raw(nano_iceoryx_puber *puber, void *msg)
{
	iox_pub_publish_chunk(puber->puber, msg);
}

void
nano_iceoryx_write(nano_iceoryx_puber *puber, void *msg)
{
	iox_pub_publish_chunk(puber->puber, msg);
}

void
nano_iceoryx_read(nano_iceoryx_suber *suber, void **msgp)
{
	if (0 != nni_lmq_get(suber->recvmq, msg)) {
		*msgp = NULL;
	}
}

