//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "iceoryx_api.h"

#include "iceoryx_binding_c/listener.h"
#include "iceoryx_binding_c/runtime.h"
#include "iceoryx_binding_c/subscriber.h"
#include "iceoryx_binding_c/types.h"
#include "iceoryx_binding_c/user_trigger.h"

int
nano_iceoryx_init()
{
    iox_runtime_init("iox-c-callback-subscriber");

    iox_listener_storage_t listenerStorage;
    iox_listener_t listener = iox_listener_init(&listenerStorage);

// Event is the topic you wanna read
int
nano_iceoryx_init(const char *const name)
{
    iox_runtime_init(name); // No related to subscriber or publisher. just a runtime name
}

int
nano_iceoryx_fini()
{
	iox_runtime_shutdown();
	return 0;
}

