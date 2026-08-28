//
// Copyright 2026 Liebherr-Digital Development Center (LDC) <peter.bestler@liebherr.de>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdlib.h>
#include <nuts.h>

/** Returns a required nonempty environment variable for PKCS#11 tests. */
static const char *
pkcs11_test_env(const char *name)
{
	const char *value = getenv(name);

	TEST_ASSERT_(value != NULL && value[0] != '\0',
	    "required environment variable %s is set", name);
	return (value);
}

/** Verifies valid PKCS#11 credentials before exercising an invalid PIN. */
void
test_pkcs11_credentials(void)
{
	const char     *cert_uri;
	const char     *key_uri;
	const char     *ca_uri;
	const char     *pin;
	nng_tls_config *server_cfg;
	nng_tls_config *client_cfg;
	nng_tls_config *invalid_cfg;

	cert_uri = pkcs11_test_env("NNG_PKCS11_CERT_URI");
	key_uri  = pkcs11_test_env("NNG_PKCS11_KEY_URI");
	ca_uri   = pkcs11_test_env("NNG_PKCS11_CA_URI");
	pin      = pkcs11_test_env("NNG_PKCS11_PIN");

	NUTS_PASS(nng_tls_config_alloc(&server_cfg, NNG_TLS_MODE_SERVER));
	NUTS_PASS(
	    nng_tls_config_own_cert(server_cfg, cert_uri, key_uri, pin));
	NUTS_PASS(nng_tls_config_alloc(&client_cfg, NNG_TLS_MODE_CLIENT));
	NUTS_PASS(nng_tls_config_ca_chain(client_cfg, ca_uri, NULL));
	nng_tls_config_free(server_cfg);
	nng_tls_config_free(client_cfg);

	NUTS_PASS(nng_tls_config_alloc(&invalid_cfg, NNG_TLS_MODE_SERVER));
	NUTS_FAIL(nng_tls_config_own_cert(
	              invalid_cfg, cert_uri, key_uri, "invalid-pin"),
	    NNG_ECRYPTO);
	nng_tls_config_free(invalid_cfg);
}

NUTS_TESTS = {
	{ "PKCS#11 credentials", test_pkcs11_credentials },
	{ NULL, NULL },
};
