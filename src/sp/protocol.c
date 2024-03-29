//
// Copyright 2021 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>

#include "core/nng_impl.h"

int
nni_proto_open(nng_socket *sip, const nni_proto *proto)
{
	int       rv;
	nni_sock *sock;

	if ((rv = nni_sock_open(&sock, proto)) == 0) {
		nng_socket s;
		s.id = nni_sock_id(sock); // Keep socket held open.
		*sip = s;
	}
	return (rv);
}

int
nni_proto_mqtt_open(nng_socket *sockidp, const nni_proto *proto,
    void (*sock_setdb)(void *, void *))
{
	int       rv;
	nni_sock *sock;

	if ((rv = nni_init()) != 0) {
		return (rv);
	}
	log_debug(" nng internal init finished! Listener start");
	if ((rv = nni_sock_open(&sock, proto)) == 0) {
		log_debug("mqtt socket opened!");
		sockidp->id = nni_sock_id(sock); // Keep socket held open.
		sock_setdb(nni_sock_proto_data(sock), sockidp->data);
	}
	return (rv);
}
