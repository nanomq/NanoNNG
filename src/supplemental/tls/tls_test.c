//
// Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <nuts.h>

void
test_tls_config_version(void)
{
	nng_tls_config *cfg;

	NUTS_PASS(nng_tls_config_alloc(&cfg, NNG_TLS_MODE_SERVER));

	// Verify that min ver < max ver
	NUTS_FAIL(nng_tls_config_version(cfg, NNG_TLS_1_3, NNG_TLS_1_0),
	    NNG_ENOTSUP);

	// Verify that we cannot configure SSL 3.0 or older.
	NUTS_FAIL(nng_tls_config_version(cfg, NNG_TLS_1_0 - 1, NNG_TLS_1_0),
	    NNG_ENOTSUP);

	// Verify that we cannot configure TLS > 1.3.
	NUTS_FAIL(nng_tls_config_version(cfg, NNG_TLS_1_0, NNG_TLS_1_3 + 1),
	    NNG_ENOTSUP);

	// Verify that we *can* configure some various ranges starting with
	// TLS v1.2.  Note that some libraries no longer support TLS 1.0
	// and TLS 1.1, so we don't test for them.
#if 0
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_0, NNG_TLS_1_0));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_0, NNG_TLS_1_1));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_0, NNG_TLS_1_2));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_0, NNG_TLS_1_3));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_1, NNG_TLS_1_1));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_1, NNG_TLS_1_2));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_1, NNG_TLS_1_3));
#endif
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_2, NNG_TLS_1_2));
	NUTS_PASS(nng_tls_config_version(cfg, NNG_TLS_1_2, NNG_TLS_1_3));

	nng_tls_config_free(cfg);
}

void
test_tls_conn_refused(void)
{
	nng_stream_dialer *dialer;
	nng_aio *          aio;

	NUTS_PASS(nng_aio_alloc(&aio, NULL, NULL));
	nng_aio_set_timeout(aio, 5000); // 5 sec

	// port 8 is generally not used for anything.
	NUTS_PASS(nng_stream_dialer_alloc(&dialer, "tls+tcp://127.0.0.1:8"));
	nng_stream_dialer_dial(dialer, aio);
	nng_aio_wait(aio);
	NUTS_FAIL(nng_aio_result(aio), NNG_ECONNREFUSED);

	nng_aio_free(aio);
	nng_stream_dialer_free(dialer);
}

/** Transfers a 450001-byte buffer over a TLS stream pair and compares it. */
void
test_tls_large_message(void)
{
	nng_stream_listener *l;
	nng_stream_dialer *  d;
	nng_aio *            aio1, *aio2;
	nng_stream *         s1;
	nng_stream *         s2;
	nng_tls_config *     c1;
	nng_tls_config *     c2;
	char                 addr[32];
	uint8_t *            buf1;
	uint8_t *            buf2;
	size_t               size = 450001;
	void *               t1;
	void *               t2;
	int                  port;

	NUTS_ENABLE_LOG(NNG_LOG_DEBUG);
	// allocate messages
	NUTS_ASSERT((buf1 = nng_alloc(size)) != NULL);
	NUTS_ASSERT((buf2 = nng_alloc(size)) != NULL);

	for (size_t i = 0; i < size; i++) {
		buf1[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio1, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&aio2, NULL, NULL));
	nng_aio_set_timeout(aio1, 5000);
	nng_aio_set_timeout(aio2, 5000);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	NUTS_PASS(nng_tls_config_own_cert(
	    c1, nuts_server_crt, nuts_server_key, NULL));
	NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, c1));
	NUTS_PASS(nng_stream_listener_listen(l));
	NUTS_PASS(
	    nng_stream_listener_get_int(l, NNG_OPT_TCP_BOUND_PORT, &port));
	NUTS_TRUE(port > 0);
	NUTS_TRUE(port < 65536);

	snprintf(addr, sizeof (addr), "tls+tcp://127.0.0.1:%d", port);
	NUTS_PASS(nng_stream_dialer_alloc(&d, addr));
	NUTS_PASS(nng_tls_config_alloc(&c2, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_ca_chain(c2, nuts_server_crt, NULL));
	NUTS_PASS(nng_tls_config_server_name(c2, "localhost"));

	NUTS_PASS(nng_stream_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, c2));

	nng_stream_listener_accept(l, aio1);
	nng_stream_dialer_dial(d, aio2);

	nng_aio_wait(aio1);
	nng_aio_wait(aio2);

	NUTS_PASS(nng_aio_result(aio1));
	NUTS_PASS(nng_aio_result(aio2));

	NUTS_TRUE((s1 = nng_aio_get_output(aio1, 0)) != NULL);
	NUTS_TRUE((s2 = nng_aio_get_output(aio2, 0)) != NULL);

	t1 = nuts_stream_send_start(s1, buf1, size);
	t2 = nuts_stream_recv_start(s2, buf2, size);

	NUTS_PASS(nuts_stream_wait(t1));
	NUTS_PASS(nuts_stream_wait(t2));
	NUTS_TRUE(memcmp(buf1, buf2, size) == 0);

	nng_free(buf1, size);
	nng_free(buf2, size);
	nng_stream_free(s1);
	nng_stream_free(s2);
	nng_stream_dialer_free(d);
	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
	nng_tls_config_free(c2);
	nng_aio_free(aio1);
	nng_aio_free(aio2);
}

/** Exercises peer authentication under TLS 1.2 and TLS 1.3. client_cert selects
 * whether the client supplies credentials; trust_client controls whether the
 * server trusts their issuer. Checks rejection errors or bidirectional data
 * transfer and verification status after the handshake. */
static void
test_tls_peer_auth(nng_tls_auth_mode mode, bool client_cert, bool trust_client)
{
	const nng_tls_version versions[] = { NNG_TLS_1_2, NNG_TLS_1_3 };

	for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); i++) {
		nng_stream_listener *l;
		nng_stream_dialer   *d;
		nng_stream         *server;
		nng_stream         *client;
		nng_tls_config     *sc;
		nng_tls_config     *cc;
		nng_aio            *accept;
		nng_aio            *dial;
		char                addr[64];
		char                sent[] = "test";
		char                recv[sizeof(sent)];
		void               *tx;
		void               *rx;
		bool                verified;
		int                 port;
		int                 rv;

		NUTS_CASE(i == 0 ? "TLS 1.2" : "TLS 1.3");
		NUTS_PASS(nng_tls_config_alloc(&sc, NNG_TLS_MODE_SERVER));
		NUTS_PASS(nng_tls_config_alloc(&cc, NNG_TLS_MODE_CLIENT));
		rv = nng_tls_config_version(sc, versions[i], versions[i]);
		if ((rv == NNG_ENOTSUP) && (versions[i] == NNG_TLS_1_3)) {
			nng_tls_config_free(sc);
			nng_tls_config_free(cc);
			continue;
		}
		NUTS_PASS(rv);
		NUTS_PASS(nng_tls_config_version(cc, versions[i], versions[i]));
		NUTS_PASS(nng_tls_config_own_cert(
		    sc, nuts_server_crt, nuts_server_key, NULL));
		if (trust_client) {
			NUTS_PASS(nng_tls_config_ca_chain(
			    sc, nuts_server_crt, NULL));
		}
		NUTS_PASS(nng_tls_config_auth_mode(sc, mode));
		NUTS_PASS(nng_tls_config_ca_chain(cc, nuts_server_crt, NULL));
		NUTS_PASS(nng_tls_config_server_name(cc, "localhost"));
		if (client_cert) {
			NUTS_PASS(nng_tls_config_own_cert(
			    cc, nuts_client_crt, nuts_client_key, NULL));
		}
		NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
		NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, sc));
		NUTS_PASS(nng_stream_listener_listen(l));
		NUTS_PASS(nng_stream_listener_get_int(
		    l, NNG_OPT_TCP_BOUND_PORT, &port));
		snprintf(addr, sizeof(addr), "tls+tcp://127.0.0.1:%d", port);
		NUTS_PASS(nng_stream_dialer_alloc(&d, addr));
		NUTS_PASS(nng_stream_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, cc));
		NUTS_PASS(nng_aio_alloc(&accept, NULL, NULL));
		NUTS_PASS(nng_aio_alloc(&dial, NULL, NULL));
		nng_aio_set_timeout(accept, 5000);
		nng_aio_set_timeout(dial, 5000);
		nng_stream_listener_accept(l, accept);
		nng_stream_dialer_dial(d, dial);
		nng_aio_wait(accept);
		nng_aio_wait(dial);
		NUTS_PASS(nng_aio_result(accept));
		NUTS_PASS(nng_aio_result(dial));
		NUTS_ASSERT((server = nng_aio_get_output(accept, 0)) != NULL);
		NUTS_ASSERT((client = nng_aio_get_output(dial, 0)) != NULL);

		// Stream connect only establishes TCP. Drive the TLS handshake
		// before asserting authentication or accepting application data.
		tx = nuts_stream_send_start(client, sent, sizeof(sent));
		rx = nuts_stream_recv_start(server, recv, sizeof(recv));
		if (!trust_client ||
		    ((mode == NNG_TLS_AUTH_MODE_REQUIRED) && !client_cert)) {
			rv = nuts_stream_wait(rx);
			NUTS_TRUE(rv == NNG_EPEERAUTH || rv == NNG_ECRYPTO);
			NUTS_MSG("expected authentication failure, got %s",
			    nng_strerror(rv));
			// TLS 1.3 may finish the client's send before the rejection
			// alert arrives; only the server must reject the data.
			nng_stream_close(client);
			(void) nuts_stream_wait(tx);
		} else {
			NUTS_PASS(nuts_stream_wait(tx));
			NUTS_PASS(nuts_stream_wait(rx));
			NUTS_TRUE(memcmp(sent, recv, sizeof(sent)) == 0);
			NUTS_PASS(nng_stream_get_bool(
			    server, NNG_OPT_TLS_VERIFIED, &verified));
			NUTS_TRUE(verified == client_cert);
			NUTS_PASS(nng_stream_get_bool(
			    client, NNG_OPT_TLS_VERIFIED, &verified));
			NUTS_TRUE(verified);

			// Check the reverse direction after both handshakes complete.
			memset(recv, 0, sizeof(recv));
			tx = nuts_stream_send_start(server, sent, sizeof(sent));
			rx = nuts_stream_recv_start(client, recv, sizeof(recv));
			NUTS_PASS(nuts_stream_wait(tx));
			NUTS_PASS(nuts_stream_wait(rx));
			NUTS_TRUE(memcmp(sent, recv, sizeof(sent)) == 0);
		}

		nng_stream_free(server);
		nng_stream_free(client);
		nng_stream_dialer_free(d);
		nng_stream_listener_free(l);
		nng_tls_config_free(sc);
		nng_tls_config_free(cc);
		nng_aio_free(accept);
		nng_aio_free(dial);
	}
}

/** Requires trusted client credentials and verifies both peers after exchange. */
void
test_tls_mutual_auth(void)
{
	test_tls_peer_auth(NNG_TLS_AUTH_MODE_REQUIRED, true, true);
}

/** Checks that REQUIRED client authentication rejects a missing certificate. */
void
test_tls_mutual_auth_missing_cert(void)
{
	test_tls_peer_auth(NNG_TLS_AUTH_MODE_REQUIRED, false, true);
}

/** Checks that REQUIRED client authentication rejects an untrusted issuer. */
void
test_tls_mutual_auth_untrusted_cert(void)
{
	test_tls_peer_auth(NNG_TLS_AUTH_MODE_REQUIRED, true, false);
}

/** Checks that OPTIONAL accepts an uncertified client and reports it unverified. */
void
test_tls_optional_without_peer_cert(void)
{
	test_tls_peer_auth(NNG_TLS_AUTH_MODE_OPTIONAL, false, true);
}

/** Checks that OPTIONAL verifies a trusted client certificate. */
void
test_tls_optional_with_peer_cert(void)
{
	test_tls_peer_auth(NNG_TLS_AUTH_MODE_OPTIONAL, true, true);
}

void
test_tls_garbled_cert(void)
{
	nng_stream_listener *l;
	nng_tls_config      *c1;

	NUTS_ENABLE_LOG(NNG_LOG_INFO);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	NUTS_FAIL(nng_tls_config_own_cert(
	              c1, nuts_garbled_crt, nuts_server_key, NULL),
	    NNG_ECRYPTO);

	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
}

void
test_tls_own_cert_trailing_garbage(void)
{
	nng_stream_listener *l;
	nng_tls_config      *c1;
	char                  certs[4096];

	NUTS_ENABLE_LOG(NNG_LOG_INFO);

	// valid leaf followed by a truncated trailing certificate (no END line)
	snprintf(certs, sizeof(certs),
	    "%s"
	    "-----BEGIN CERTIFICATE-----\n"
	    "MIIDdzCCAl8CFEzqJgxMn+OTdw7RjLtz8FlhrQ0HMA0GCSqGSIb3DQEBCwUAMHcx\n",
	    nuts_server_crt);

	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	NUTS_FAIL(nng_tls_config_own_cert(c1, certs, nuts_server_key, NULL),
	    NNG_ECRYPTO);

	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
}

void
test_tls_psk(void)
{
	nng_stream_listener *l;
	nng_stream_dialer   *d;
	nng_aio             *aio1, *aio2;
	nng_stream          *s1;
	nng_stream          *s2;
	nng_tls_config      *c1;
	nng_tls_config      *c2;
	char                 addr[32];
	uint8_t              key[32];
	uint8_t             *buf1;
	uint8_t             *buf2;
	size_t               size = 10000;
	void                *t1;
	void                *t2;
	int                  port;

	NUTS_ENABLE_LOG(NNG_LOG_DEBUG);
	// allocate messages
	NUTS_ASSERT((buf1 = nng_alloc(size)) != NULL);
	NUTS_ASSERT((buf2 = nng_alloc(size)) != NULL);

	for (size_t i = 0; i < sizeof(key); i++) {
		key[i] = rand() & 0xff;
	}
	for (size_t i = 0; i < size; i++) {
		buf1[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio1, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&aio2, NULL, NULL));
	nng_aio_set_timeout(aio1, 5000);
	nng_aio_set_timeout(aio2, 5000);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	NUTS_PASS(nng_tls_config_psk(c1, "identity", key, sizeof(key)));
	NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, c1));
	NUTS_PASS(nng_stream_listener_listen(l));
	NUTS_PASS(
	    nng_stream_listener_get_int(l, NNG_OPT_TCP_BOUND_PORT, &port));
	NUTS_TRUE(port > 0);
	NUTS_TRUE(port < 65536);

	snprintf(addr, sizeof(addr), "tls+tcp://127.0.0.1:%d", port);
	NUTS_PASS(nng_stream_dialer_alloc(&d, addr));
	NUTS_PASS(nng_tls_config_alloc(&c2, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_psk(c2, "identity", key, sizeof(key)));
	NUTS_PASS(nng_tls_config_server_name(c2, "localhost"));

	NUTS_PASS(nng_stream_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, c2));

	nng_stream_listener_accept(l, aio1);
	nng_stream_dialer_dial(d, aio2);

	nng_aio_wait(aio1);
	nng_aio_wait(aio2);

	NUTS_PASS(nng_aio_result(aio1));
	NUTS_PASS(nng_aio_result(aio2));

	NUTS_TRUE((s1 = nng_aio_get_output(aio1, 0)) != NULL);
	NUTS_TRUE((s2 = nng_aio_get_output(aio2, 0)) != NULL);

	t1 = nuts_stream_send_start(s1, buf1, size);
	t2 = nuts_stream_recv_start(s2, buf2, size);

	NUTS_PASS(nuts_stream_wait(t1));
	NUTS_PASS(nuts_stream_wait(t2));
	NUTS_TRUE(memcmp(buf1, buf2, size) == 0);

	nng_free(buf1, size);
	nng_free(buf2, size);
	nng_stream_free(s1);
	nng_stream_free(s2);
	nng_stream_dialer_free(d);
	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
	nng_tls_config_free(c2);
	nng_aio_free(aio1);
	nng_aio_free(aio2);
}

void
test_tls_psk_server_identities(void)
{
	nng_stream_listener *l;
	nng_stream_dialer   *d;
	nng_aio             *aio1, *aio2;
	nng_stream          *s1;
	nng_stream          *s2;
	nng_tls_config      *c1;
	nng_tls_config      *c2;
	char                 addr[32];
	uint8_t             *buf1;
	uint8_t             *buf2;
	size_t               size = 10000;
	void                *t1;
	void                *t2;
	int                  port;
	char                *identity = "test_identity";
	uint8_t              key[32];

	NUTS_ENABLE_LOG(NNG_LOG_INFO);
	// allocate messages
	NUTS_ASSERT((buf1 = nng_alloc(size)) != NULL);
	NUTS_ASSERT((buf2 = nng_alloc(size)) != NULL);

	for (size_t i = 0; i < sizeof(key); i++) {
		key[i] = rand() & 0xff;
	}
	for (size_t i = 0; i < size; i++) {
		buf1[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio1, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&aio2, NULL, NULL));
	nng_aio_set_timeout(aio1, 5000);
	nng_aio_set_timeout(aio2, 5000);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	// Replace the identity .. first write one value, then we change it
	NUTS_PASS(
	    nng_tls_config_psk(c1, "identity2", key + 4, sizeof(key) - 4));
	NUTS_PASS(nng_tls_config_psk(c1, identity, key + 4, sizeof(key) - 4));
	NUTS_PASS(nng_tls_config_psk(c1, identity, key, sizeof(key)));
	NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, c1));
	NUTS_PASS(nng_stream_listener_listen(l));
	NUTS_PASS(
	    nng_stream_listener_get_int(l, NNG_OPT_TCP_BOUND_PORT, &port));
	NUTS_TRUE(port > 0);
	NUTS_TRUE(port < 65536);

	snprintf(addr, sizeof(addr), "tls+tcp://127.0.0.1:%d", port);
	NUTS_PASS(nng_stream_dialer_alloc(&d, addr));
	NUTS_PASS(nng_tls_config_alloc(&c2, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_psk(c2, identity, key, sizeof(key)));
	NUTS_PASS(nng_tls_config_server_name(c2, "localhost"));

	NUTS_PASS(nng_stream_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, c2));

	nng_stream_listener_accept(l, aio1);
	nng_stream_dialer_dial(d, aio2);

	nng_aio_wait(aio1);
	nng_aio_wait(aio2);

	NUTS_PASS(nng_aio_result(aio1));
	NUTS_PASS(nng_aio_result(aio2));

	NUTS_TRUE((s1 = nng_aio_get_output(aio1, 0)) != NULL);
	NUTS_TRUE((s2 = nng_aio_get_output(aio2, 0)) != NULL);

	t1 = nuts_stream_send_start(s1, buf1, size);
	t2 = nuts_stream_recv_start(s2, buf2, size);

	NUTS_PASS(nuts_stream_wait(t1));
	NUTS_PASS(nuts_stream_wait(t2));
	NUTS_TRUE(memcmp(buf1, buf2, size) == 0);

	nng_free(buf1, size);
	nng_free(buf2, size);
	nng_stream_free(s1);
	nng_stream_free(s2);
	nng_stream_dialer_free(d);
	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
	nng_tls_config_free(c2);
	nng_aio_free(aio1);
	nng_aio_free(aio2);
}

void
test_tls_psk_bad_identity(void)
{
	nng_stream_listener *l;
	nng_stream_dialer   *d;
	nng_aio             *aio1, *aio2;
	nng_stream          *s1;
	nng_stream          *s2;
	nng_tls_config      *c1;
	nng_tls_config      *c2;
	char                 addr[32];
	uint8_t             *buf1;
	uint8_t             *buf2;
	size_t               size = 10000;
	void                *t1;
	void                *t2;
	int                  port;
	uint8_t              key[32];

	NUTS_ENABLE_LOG(NNG_LOG_INFO);
	// allocate messages
	NUTS_ASSERT((buf1 = nng_alloc(size)) != NULL);
	NUTS_ASSERT((buf2 = nng_alloc(size)) != NULL);

	for (size_t i = 0; i < sizeof(key); i++) {
		key[i] = rand() & 0xff;
	}
	for (size_t i = 0; i < size; i++) {
		buf1[i] = rand() & 0xff;
	}

	NUTS_PASS(nng_aio_alloc(&aio1, NULL, NULL));
	NUTS_PASS(nng_aio_alloc(&aio2, NULL, NULL));
	nng_aio_set_timeout(aio1, 5000);
	nng_aio_set_timeout(aio2, 5000);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	// Replace the identity .. first write one value, then we change it
	NUTS_PASS(nng_tls_config_psk(c1, "identity1", key, sizeof(key)));
	NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, c1));
	NUTS_PASS(nng_stream_listener_listen(l));
	NUTS_PASS(
	    nng_stream_listener_get_int(l, NNG_OPT_TCP_BOUND_PORT, &port));
	NUTS_TRUE(port > 0);
	NUTS_TRUE(port < 65536);

	snprintf(addr, sizeof(addr), "tls+tcp://127.0.0.1:%d", port);
	NUTS_PASS(nng_stream_dialer_alloc(&d, addr));
	NUTS_PASS(nng_tls_config_alloc(&c2, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_psk(c2, "identity2", key, sizeof(key)));
	NUTS_PASS(nng_tls_config_server_name(c2, "localhost"));

	NUTS_PASS(nng_stream_dialer_set_ptr(d, NNG_OPT_TLS_CONFIG, c2));

	nng_stream_listener_accept(l, aio1);
	nng_stream_dialer_dial(d, aio2);

	nng_aio_wait(aio1);
	nng_aio_wait(aio2);

	NUTS_PASS(nng_aio_result(aio1));
	NUTS_PASS(nng_aio_result(aio2));

	NUTS_TRUE((s1 = nng_aio_get_output(aio1, 0)) != NULL);
	NUTS_TRUE((s2 = nng_aio_get_output(aio2, 0)) != NULL);

	t1 = nuts_stream_send_start(s1, buf1, size);
	t2 = nuts_stream_recv_start(s2, buf2, size);

	// These can fail due to ECRYPTO, EPEERAUTH, or ECONNSHUT, for example
	NUTS_ASSERT(nuts_stream_wait(t1) != 0);
	NUTS_ASSERT(nuts_stream_wait(t2) != 0);

	nng_free(buf1, size);
	nng_free(buf2, size);
	nng_stream_free(s1);
	nng_stream_free(s2);
	nng_stream_dialer_free(d);
	nng_stream_listener_free(l);
	nng_tls_config_free(c1);
	nng_tls_config_free(c2);
	nng_aio_free(aio1);
	nng_aio_free(aio2);
}

void
test_tls_psk_key_too_big(void)
{
	nng_tls_config *c1;
	uint8_t         key[5000];

	NUTS_ENABLE_LOG(NNG_LOG_INFO);

	// Allocate the listener first.  We use a wild-card port.
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_CLIENT));
	NUTS_FAIL(
	    nng_tls_config_psk(c1, "identity", key, sizeof(key)), NNG_ECRYPTO);
	nng_tls_config_free(c1);
}

void
test_tls_psk_config_busy(void)
{
	nng_tls_config      *c1;
	uint8_t              key[32];
	nng_stream_listener *l;
	nng_aio             *aio;

	nng_aio_alloc(&aio, NULL, NULL);

	NUTS_ENABLE_LOG(NNG_LOG_INFO);

	NUTS_PASS(nng_stream_listener_alloc(&l, "tls+tcp://127.0.0.1:0"));
	NUTS_PASS(nng_tls_config_alloc(&c1, NNG_TLS_MODE_SERVER));
	NUTS_PASS(nng_tls_config_psk(c1, "identity", key, sizeof(key)));
	NUTS_PASS(nng_stream_listener_set_ptr(l, NNG_OPT_TLS_CONFIG, c1));
	nng_stream_listener_accept(l, aio);
	nng_msleep(100);
	NUTS_FAIL(
	    nng_tls_config_psk(c1, "identity2", key, sizeof(key)), NNG_EBUSY);

	nng_stream_listener_free(l);
	nng_aio_free(aio);
	nng_tls_config_free(c1);
}

TEST_LIST = {
	{ "tls config version", test_tls_config_version },
	{ "tls conn refused", test_tls_conn_refused },
	{ "tls large message", test_tls_large_message },
	{ "tls mutual auth", test_tls_mutual_auth },
	{ "tls mutual auth missing cert", test_tls_mutual_auth_missing_cert },
	{ "tls mutual auth untrusted cert", test_tls_mutual_auth_untrusted_cert },
	{ "tls optional without peer cert", test_tls_optional_without_peer_cert },
	{ "tls optional with peer cert", test_tls_optional_with_peer_cert },
	{ "tls garbled cert", test_tls_garbled_cert },
	{ "tls own cert trailing garbage", test_tls_own_cert_trailing_garbage },
	{ "tls psk", test_tls_psk },
	{ "tls psk server identities", test_tls_psk_server_identities },
	{ "tls psk bad identity", test_tls_psk_bad_identity },
	{ "tls psk key too big", test_tls_psk_key_too_big },
	{ "tls psk key config busy", test_tls_psk_config_busy },
	{ NULL, NULL },
};
