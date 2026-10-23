// This software is supplied under the MIT License (LICENSE.txt).

#include <nuts.h>

#include "tls_peer_identity_certs.h"

/** Completes a loopback TLS handshake and transfers authenticated data. */
static int
tls_peer_result(const char *name, const char *ca, nng_tls_auth_mode auth,
    nng_tls_version version)
{
	nng_stream_listener *listener;
	nng_stream_dialer   *dialer;
	nng_tls_config      *server_cfg, *client_cfg;
	nng_aio             *accept_aio, *dial_aio;
	nng_stream          *server = NULL, *client = NULL;
	nng_iov              send_iov, recv_iov;
	char                 sent = 'x', received = 0;
	char                 addr[64];
	int                  port, result;
	bool                 verified;

	NUTS_PASS(nng_stream_listener_alloc(
	    &listener, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&server_cfg, NNG_TLS_MODE_SERVER));
	NUTS_PASS(nng_tls_config_version(server_cfg, version, version));
	NUTS_PASS(nng_tls_config_own_cert(
	    server_cfg, peer_server_cert, peer_server_key, NULL));
	NUTS_PASS(nng_stream_listener_set_ptr(
	    listener, NNG_OPT_TLS_CONFIG, server_cfg));
	NUTS_PASS(nng_stream_listener_listen(listener));
	NUTS_PASS(nng_stream_listener_get_int(
	    listener, NNG_OPT_TCP_BOUND_PORT, &port));
	snprintf(addr, sizeof(addr), "tls+tcp://127.0.0.1:%d", port);
	NUTS_PASS(nng_stream_dialer_alloc(&dialer, addr));
	NUTS_PASS(nng_tls_config_alloc(&client_cfg, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_version(client_cfg, version, version));
	NUTS_PASS(nng_tls_config_auth_mode(client_cfg, auth));
	NUTS_PASS(nng_tls_config_ca_chain(client_cfg, ca, NULL));
	if (name != NULL) {
		NUTS_PASS(nng_tls_config_server_name(client_cfg, name));
	}
	NUTS_PASS(nng_stream_dialer_set_ptr(
	    dialer, NNG_OPT_TLS_CONFIG, client_cfg));
	NUTS_PASS(nng_aio_alloc(&accept_aio, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&dial_aio, NULL, NULL));
	nng_aio_set_timeout(accept_aio, 3000);
	nng_aio_set_timeout(dial_aio, 3000);
	nng_stream_listener_accept(listener, accept_aio);
	nng_stream_dialer_dial(dialer, dial_aio);
	nng_aio_wait(dial_aio);
	result = nng_aio_result(dial_aio);
	if (result == 0) {
		client = nng_aio_get_output(dial_aio, 0);
		nng_aio_wait(accept_aio);
		NUTS_PASS(nng_aio_result(accept_aio));
		server = nng_aio_get_output(accept_aio, 0);
		// Establishing a stream alone does not authenticate the peer.
		send_iov.iov_buf = &sent;
		send_iov.iov_len = 1;
		recv_iov.iov_buf = &received;
		recv_iov.iov_len = 1;
		NUTS_PASS(nng_aio_set_iov(dial_aio, 1, &send_iov));
		NUTS_PASS(nng_aio_set_iov(accept_aio, 1, &recv_iov));
		nng_stream_recv(server, accept_aio);
		nng_stream_send(client, dial_aio);
		nng_aio_wait(dial_aio);
		result = nng_aio_result(dial_aio);
		if (result == 0) {
			nng_aio_wait(accept_aio);
			NUTS_PASS(nng_aio_result(accept_aio));
			NUTS_TRUE(received == sent);
			if (auth != NNG_TLS_AUTH_MODE_NONE) {
				NUTS_PASS(nng_stream_get_bool(
				    client, NNG_OPT_TLS_VERIFIED, &verified));
				NUTS_TRUE(verified);
			}
		} else {
			// A rejected peer must not deliver application data.
			NUTS_TRUE(received == 0);
		}
		nng_stream_close(client);
		nng_stream_close(server);
	}
	nng_stream_listener_close(listener);
	nng_aio_wait(accept_aio);
	if (server == NULL && nng_aio_result(accept_aio) == 0) {
		server = nng_aio_get_output(accept_aio, 0);
	}
	nng_stream_free(server);
	nng_stream_free(client);
	nng_aio_free(accept_aio);
	nng_aio_free(dial_aio);
	nng_stream_dialer_free(dialer);
	nng_stream_listener_free(listener);
	nng_tls_config_free(server_cfg);
	nng_tls_config_free(client_cfg);
	return (result);
}

/** Checks DNS and IP identity matching under TLS 1.2 and TLS 1.3. */
static void
test_tls_peer_identity(void)
{
	const char *matches[] = { "server.test", "SERVER.TEST", "127.0.0.1", "::1" };
	const char *mismatches[] = { "wrong.test", "127.0.0.2", "::2" };

	for (nng_tls_version version = NNG_TLS_1_2; version <= NNG_TLS_1_3;
	     version++) {
		NUTS_CASE(version == NNG_TLS_1_2 ? "TLS 1.2" : "TLS 1.3");
		for (size_t i = 0; i < sizeof(matches) / sizeof(matches[0]); i++) {
			NUTS_PASS(tls_peer_result(matches[i], peer_ca_cert,
			    NNG_TLS_AUTH_MODE_REQUIRED, version));
		}
		for (size_t i = 0; i < sizeof(mismatches) / sizeof(mismatches[0]);
		     i++) {
			NUTS_FAIL(tls_peer_result(mismatches[i], peer_ca_cert,
			    NNG_TLS_AUTH_MODE_REQUIRED, version), NNG_ECRYPTO);
		}
		NUTS_FAIL(tls_peer_result("server.test", nuts_server_crt,
		    NNG_TLS_AUTH_MODE_REQUIRED, version), NNG_ECRYPTO);
	}
}

/** Preserves explicitly disabled authentication and unnamed custom configs. */
static void
test_tls_peer_identity_auth_modes(void)
{
	for (nng_tls_version version = NNG_TLS_1_2; version <= NNG_TLS_1_3;
	     version++) {
		NUTS_CASE(version == NNG_TLS_1_2 ? "TLS 1.2" : "TLS 1.3");
		NUTS_PASS(tls_peer_result("wrong.test", peer_ca_cert,
		    NNG_TLS_AUTH_MODE_NONE, version));
		NUTS_FAIL(tls_peer_result("wrong.test", peer_ca_cert,
		    NNG_TLS_AUTH_MODE_OPTIONAL, version), NNG_ECRYPTO);
		NUTS_PASS(tls_peer_result(NULL, peer_ca_cert,
		    NNG_TLS_AUTH_MODE_REQUIRED, version));
		NUTS_PASS(tls_peer_result("", peer_ca_cert,
		    NNG_TLS_AUTH_MODE_REQUIRED, version));
	}
}

TEST_LIST = {
	{ "tls peer identity", test_tls_peer_identity },
	{ "tls peer identity auth modes", test_tls_peer_identity_auth_modes },
	{ NULL, NULL },
};
