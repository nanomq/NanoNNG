//
// Copyright 2024 NanoMQ Team, Inc. <wangwei@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifdef __cplusplus
extern "C" {
#endif

#if defined(SUPP_ICEORYX)

NNG_DECL int nng_iceoryx_open(nng_socket *sock, const char *runtimename);

#endif
#ifdef __cplusplus
}
#endif

#endif

