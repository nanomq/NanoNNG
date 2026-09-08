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

/** Returns an optional environment variable, or NULL when unset or empty. */
static const char *
pkcs11_test_env_opt(const char *name)
{
	const char *value = getenv(name);

	return (((value != NULL) && (value[0] != '\0')) ? value : NULL);
}

/** Reads a whole file into a NUL-terminated buffer the caller frees. */
static char *
pkcs11_test_read_file(const char *path)
{
	FILE  *fp;
	char  *buf;
	long   len;
	size_t got;

	if ((fp = fopen(path, "rb")) == NULL) {
		return (NULL);
	}
	if ((fseek(fp, 0, SEEK_END) != 0) || ((len = ftell(fp)) < 0) ||
	    (fseek(fp, 0, SEEK_SET) != 0)) {
		fclose(fp);
		return (NULL);
	}
	if ((buf = malloc((size_t) len + 1)) == NULL) {
		fclose(fp);
		return (NULL);
	}
	got = fread(buf, 1, (size_t) len, fp);
	fclose(fp);
	if (got != (size_t) len) {
		free(buf);
		return (NULL);
	}
	buf[len] = '\0';
	return (buf);
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

/**
 * Verifies a PEM certificate paired with a PKCS#11 private key.
 *
 * This is the common HSM deployment: the certificate stays in the
 * filesystem while only the key lives on the token. Requires
 * NNG_PKCS11_CERT_PEM to name a PEM file holding the certificate that
 * belongs to NNG_PKCS11_KEY_URI; the check is skipped when that fixture is
 * not configured.
 */
void
test_pkcs11_mixed_credentials(void)
{
	const char     *cert_path;
	const char     *key_uri;
	const char     *pin;
	char           *cert_pem;
	nng_tls_config *cfg;

	if ((cert_path = pkcs11_test_env_opt("NNG_PKCS11_CERT_PEM")) == NULL) {
		return;
	}
	key_uri = pkcs11_test_env("NNG_PKCS11_KEY_URI");
	pin     = pkcs11_test_env("NNG_PKCS11_PIN");

	cert_pem = pkcs11_test_read_file(cert_path);
	TEST_ASSERT_(cert_pem != NULL, "certificate file %s is readable",
	    cert_path);

	NUTS_PASS(nng_tls_config_alloc(&cfg, NNG_TLS_MODE_SERVER));
	NUTS_PASS(nng_tls_config_own_cert(cfg, cert_pem, key_uri, pin));
	nng_tls_config_free(cfg);
	free(cert_pem);
}

NUTS_TESTS = {
	{ "PKCS#11 credentials", test_pkcs11_credentials },
	{ "PKCS#11 mixed credentials", test_pkcs11_mixed_credentials },
	{ NULL, NULL },
};
