//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#if defined(SUPP_ICEORYX)

#ifdef __cplusplus
extern "C" {
#endif

struct nng_iceoryx_suber {
	nano_iceoryx_suber *suber;
};
typedef struct nng_iceoryx_suber nng_iceoryx_suber;

NNG_DECL int nng_iceoryx_open(nng_socket *sock, const char *runtimename);

NNG_DECL int nng_iceoryx_sub(nng_socket *sock, const char *subername,
    const char *const service_name, const char *const instance_name,
    const char *const event, nng_iceoryx_suber **nsp)

#ifdef __cplusplus
}
#endif

#endif

