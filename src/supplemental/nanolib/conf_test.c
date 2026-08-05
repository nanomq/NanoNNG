// Copyright 2026 Liebherr-Digital Development Center (LDC) <peter.bestler@liebherr.de>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/nanolib.h"

#include "nuts.h"

#if defined(NANOMQ_TEST_CONF_DIR)
#define OLD_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_old_test.conf"
#define CONF_PATH NANOMQ_TEST_CONF_DIR "/nmq_test.conf"
#define PKCS11_OLD_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_tls_pkcs11_test.conf"
#elif defined(ENABLE_NANOMQ_TESTS)
#define OLD_CONF_PATH                                            \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_old_test.conf"
#define CONF_PATH \
	"../../../../../nng/src/supplemental/nanolib/test_conf/nmq_test.conf"
#define PKCS11_OLD_CONF_PATH                                    \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH                                         \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_test.conf"
#else
#define OLD_CONF_PATH \
	"../../../../src/supplemental/nanolib/test_conf/nmq_old_test.conf"
#define CONF_PATH \
	"../../../../src/supplemental/nanolib/test_conf/nmq_test.conf"
#define PKCS11_OLD_CONF_PATH                               \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH                                   \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_test.conf"
#endif

#define PKCS11_KEY_URI \
	"pkcs11:token=NanoMQ;object=broker-key;type=private"
#define PKCS11_CERT_URI \
	"pkcs11:token=NanoMQ;object=broker-cert;type=cert"
#define PKCS11_CA_URI \
	"pkcs11:token=NanoMQ;object=broker-ca;type=cert"

conf *
get_test_conf(const char *conf_path)
{
	conf *nmq_conf = nng_zalloc(sizeof(conf));
	if(nmq_conf == NULL) {
		return NULL;
	}
	conf_init(nmq_conf);
	nmq_conf->conf_file = nng_strdup(conf_path);

	return nmq_conf;
}

void
test_get_size(void)
{
	char    *str_size1   = "3KB";
	char    *str_size2   = "2MB";
	char    *str_size3   = "1GB";
	char    *str_size_tb = "1TB";
	char    *str_size_0  = "0s";
	uint64_t size        = 0;

	NUTS_PASS(get_size(str_size1, &size));
	NUTS_TRUE(size == 3 * 1024);
	NUTS_PASS(get_size(str_size2, &size));
	NUTS_TRUE(size == 2 * 1024 * 1024);
	NUTS_PASS(get_size(str_size3, &size));
	NUTS_TRUE(size == 1 * 1024 * 1024 * 1024);

	NUTS_FAIL(get_size(str_size_tb, &size), -1);
	NUTS_FAIL(get_size(str_size_0, &size), -1);
}

void
test_get_time(void)
{
	char    *str_time_s = "12s";
	char    *str_time_m = "24m";
	char    *str_time_h = "6h";
	char    *str_dflt   = "12dflt";
	char    *str_fail   = "fail";
	uint64_t second     = 0;

	NUTS_PASS(get_time(str_time_s, &second));
	NUTS_TRUE(second == 12);
	NUTS_PASS(get_time(str_time_m, &second));
	NUTS_TRUE(second == 24 * 60);
	NUTS_PASS(get_time(str_time_h, &second));
	NUTS_TRUE(second == 6 * 3600);

	// TODO: Should this be considered as fail?
	NUTS_PASS(get_time(str_dflt, &second));

	NUTS_FAIL(get_time(str_fail, &second), -1);
}

void
test_conf_parse(void)
{
#ifndef NNG_PLATFORM_WINDOWS
	conf *conf = get_test_conf(OLD_CONF_PATH);
	NUTS_TRUE(conf != NULL);
	if (conf == NULL) {
		return;
	}
	conf_parse(conf);
	NUTS_TRUE(conf->url != NULL);
	if (conf->url != NULL) {
		NUTS_TRUE(strncmp(conf->url, "nmq-tcp://0.0.0.0:1883", 22) == 0);
		print_conf(conf);
	}

	conf_fini(conf);
#endif
}

void
test_conf_parse_ver2(void)
{
	conf *conf = get_test_conf(CONF_PATH);
	NUTS_TRUE(conf != NULL);
	if (conf == NULL) {
		return;
	}
	conf_parse_ver2(conf, false);
	NUTS_TRUE(conf->url != NULL);
	if (conf->url != NULL) {
		NUTS_TRUE(strncmp(conf->url, "nmq-tcp://0.0.0.0:1883", 22) == 0);
		print_conf(conf);
	}

	conf_fini(conf);
}

void
test_get_size_edge_cases(void)
{
	uint64_t size = 0;
	char *str_lower = "5kb";
	char *str_mixed = "10Mb";
	char *str_no_unit = "100";

	// Test case-insensitive parsing
	NUTS_PASS(get_size(str_lower, &size));
	NUTS_TRUE(size == 5 * 1024);

	NUTS_PASS(get_size(str_mixed, &size));
	NUTS_TRUE(size == 10 * 1024 * 1024);

	NUTS_PASS(get_size(str_no_unit, &size));
	NUTS_TRUE(size == 100);
}

void
test_get_size_null_params(void)
{
	uint64_t size = 0;
	char *str_size = "1KB";

	// Test NULL string
	NUTS_FAIL(get_size(NULL, &size), -1);

	// Test NULL size pointer
	NUTS_FAIL(get_size(str_size, NULL), -1);
}

void
test_get_size_invalid_formats(void)
{
	uint64_t size = 0;
	char *str_empty = "";
	char *str_only_unit = "KB";
	char *str_negative = "-5KB";
	char *str_special = "5@KB";

	NUTS_FAIL(get_size(str_empty, &size), -1);
	NUTS_FAIL(get_size(str_only_unit, &size), -1);
	NUTS_FAIL(get_size(str_negative, &size), -1);
	NUTS_FAIL(get_size(str_special, &size), -1);
}

void
test_get_size_boundary_values(void)
{
	uint64_t size = 0;
	char *str_zero_kb = "0KB";
	char *str_max = "1000GB";

	// Zero size
	NUTS_PASS(get_size(str_zero_kb, &size));
	NUTS_TRUE(size == 0);

	// Large size
	NUTS_PASS(get_size(str_max, &size));
	NUTS_TRUE(size == 1000ULL * 1024 * 1024 * 1024);
}

void
test_get_time_ms(void)
{
	uint64_t ms = 0;
	char *str_ms = "500ms";
	char *str_s = "2s";
	char *str_m = "3m";

	// Test milliseconds
	NUTS_PASS(get_time_ms(str_ms, &ms));
	NUTS_TRUE(ms == 500);

	// Test seconds to ms conversion
	NUTS_PASS(get_time_ms(str_s, &ms));
	NUTS_TRUE(ms == 2000);

	// Test minutes to ms conversion
	NUTS_PASS(get_time_ms(str_m, &ms));
	NUTS_TRUE(ms == 3 * 60 * 1000);
}

void
test_get_time_edge_cases(void)
{
	uint64_t second = 0;
	char *str_zero_s = "0s";
	char *str_zero_m = "0m";
	char *str_zero_h = "0h";

	// Zero values
	NUTS_PASS(get_time(str_zero_s, &second));
	NUTS_TRUE(second == 0);

	NUTS_PASS(get_time(str_zero_m, &second));
	NUTS_TRUE(second == 0);

	NUTS_PASS(get_time(str_zero_h, &second));
	NUTS_TRUE(second == 0);
}

void
test_get_time_invalid_formats(void)
{
	uint64_t second = 0;
	char *str_empty = "";
	char *str_only_unit = "s";
	char *str_negative = "-10s";

	NUTS_FAIL(get_time(str_empty, &second), -1);
	NUTS_FAIL(get_time(str_only_unit, &second), -1);
	NUTS_FAIL(get_time(str_negative, &second), -1);
}

void
test_get_time_null_params(void)
{
	uint64_t second = 0;
	char *str_time = "10s";

	// Test NULL string
	NUTS_FAIL(get_time(NULL, &second), -1);

	// Test NULL second pointer
	NUTS_FAIL(get_time(str_time, NULL), -1);
}

void
test_conf_init(void)
{
	conf *nmq_conf = nng_zalloc(sizeof(conf));
	NUTS_TRUE(nmq_conf != NULL);

	conf_init(nmq_conf);

	// Verify default values are set
	NUTS_TRUE(nmq_conf->enable == true);
	NUTS_TRUE(nmq_conf->parallel > 0);
	NUTS_TRUE(nmq_conf->property_size > 0);

	conf_fini(nmq_conf);
}

void
test_conf_init_null(void)
{
	// Test with NULL conf - should handle gracefully or crash
	// This documents expected behavior
	// conf_init(NULL); // Would crash, don't test
}

void
test_conf_fini_null(void)
{
	// Test with NULL conf - should handle gracefully
	conf_fini(NULL);
}

void
test_get_size_bytes_unit(void)
{
	uint64_t size = 0;
	char *str_size_b = "1024B";

	// Some implementations support 'B' for bytes
	int rv = get_size(str_size_b, &size);
	if (rv == 0) {
		NUTS_TRUE(size == 1024);
	}
	// If not supported, should fail gracefully
}

void
test_get_time_days(void)
{
	uint64_t second = 0;
	char *str_time_d = "2d";

	// Some implementations might support days
	int rv = get_time(str_time_d, &second);
	if (rv == 0) {
		NUTS_TRUE(second == 2 * 24 * 3600);
	}
	// If not supported, should fail gracefully
}

void
test_conf_parse_tls_pkcs11_old(void)
{
	conf_tls tls;
	conf_tls_init(&tls);
	conf_tls_parse(&tls, PKCS11_OLD_CONF_PATH, "\0", "\0");

	NUTS_TRUE(NULL != tls.keyfile);
	NUTS_TRUE(NULL != tls.key);
	NUTS_TRUE(strcmp(tls.keyfile, PKCS11_KEY_URI) == 0);
	NUTS_TRUE(0 == strcmp(tls.key, tls.keyfile));

	NUTS_TRUE(NULL != tls.certfile);
	NUTS_TRUE(NULL != tls.cert);
	NUTS_TRUE(strcmp(tls.certfile, PKCS11_CERT_URI) == 0);
	NUTS_TRUE(0 == strcmp(tls.cert, tls.certfile));
	NUTS_TRUE(NULL != tls.cafile);
	NUTS_TRUE(NULL != tls.ca);
	NUTS_TRUE(strcmp(tls.cafile, PKCS11_CA_URI) == 0);
	NUTS_TRUE(0 == strcmp(tls.ca, tls.cafile));

	conf_tls_destroy(&tls);
}

void
test_conf_parse_ver2_tls_pkcs11(void)
{
	conf *conf = get_test_conf(PKCS11_CONF_PATH);
	NUTS_TRUE(conf != NULL);
	conf_parse_ver2(conf, false);

	NUTS_TRUE(NULL != conf->tls.url);
	NUTS_TRUE(
	    0 == strcmp(conf->tls.url, "tls+nmq-tcp://0.0.0.0:8883"));

	NUTS_TRUE(NULL != conf->tls.keyfile);
	NUTS_TRUE(NULL != conf->tls.key);
	NUTS_TRUE(strcmp(conf->tls.keyfile, PKCS11_KEY_URI) == 0);
	NUTS_TRUE(0 == strcmp(conf->tls.key, conf->tls.keyfile));

	NUTS_TRUE(NULL != conf->tls.certfile);
	NUTS_TRUE(NULL != conf->tls.cert);
	NUTS_TRUE(strcmp(conf->tls.certfile, PKCS11_CERT_URI) == 0);
	NUTS_TRUE(0 == strcmp(conf->tls.cert, conf->tls.certfile));
	NUTS_TRUE(NULL != conf->tls.cafile);
	NUTS_TRUE(NULL != conf->tls.ca);
	NUTS_TRUE(strcmp(conf->tls.cafile, PKCS11_CA_URI) == 0);
	NUTS_TRUE(0 == strcmp(conf->tls.ca, conf->tls.cafile));

	conf_fini(conf);
}

void
test_conf_tls_is_pkcs11_uri(void)
{
	NUTS_TRUE(conf_tls_is_pkcs11_uri(PKCS11_KEY_URI));
	NUTS_TRUE(conf_tls_is_pkcs11_uri(
	    "PKCS11:token=NanoMQ;object=broker-key;type=private"));
	NUTS_TRUE(!conf_tls_is_pkcs11_uri("/etc/certs/key.pem"));
	NUTS_TRUE(!conf_tls_is_pkcs11_uri(NULL));
}

NUTS_TESTS = {
   {"get size", test_get_size},
   {"get time", test_get_time},
   {"conf parse v2", test_conf_parse_ver2},
   {"conf parse v2 pkcs11", test_conf_parse_ver2_tls_pkcs11},
   {"conf parse", test_conf_parse},
   {"get size edge cases", test_get_size_edge_cases},
   {"get size null params", test_get_size_null_params},
   {"get size invalid formats", test_get_size_invalid_formats},
   {"get size boundary values", test_get_size_boundary_values},
   {"get time ms", test_get_time_ms},
   {"get time edge cases", test_get_time_edge_cases},
   {"get time invalid formats", test_get_time_invalid_formats},
   {"get time null params", test_get_time_null_params},
   {"conf init", test_conf_init},
   {"conf init null", test_conf_init_null},
   {"conf fini null", test_conf_fini_null},
   {"get size bytes unit", test_get_size_bytes_unit},
   {"get time days", test_get_time_days},
   {"conf parse pkcs11", test_conf_parse_tls_pkcs11_old},
   {"identify pkcs11 URIs", test_conf_tls_is_pkcs11_uri},
   {NULL, NULL}
};
