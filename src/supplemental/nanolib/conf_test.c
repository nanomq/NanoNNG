// Copyright 2026 Liebherr-Digital Development Center (LDC) <peter.bestler@liebherr.de>
// Copyright 2026 NanoMQ Team, Inc.
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/nanolib.h"
#include "nng/supplemental/nanolib/cJSON.h"

#include "nuts.h"

#if defined(NANOMQ_TEST_CONF_DIR)
#define OLD_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_old_test.conf"
#define CONF_PATH NANOMQ_TEST_CONF_DIR "/nmq_test.conf"
#define BRIDGE_CACHE_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_bridge_cache_test.conf"
#define PKCS11_OLD_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_tls_pkcs11_test.conf"
#define PEER_CERT_CONF_PATH \
	NANOMQ_TEST_CONF_DIR "/nmq_tls_peer_cert_test.conf"
#elif defined(ENABLE_NANOMQ_TESTS)
#define OLD_CONF_PATH                                            \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_old_test.conf"
#define CONF_PATH \
	"../../../../../nng/src/supplemental/nanolib/test_conf/nmq_test.conf"
#define BRIDGE_CACHE_CONF_PATH                                          \
	"../../../../../nng/src/supplemental/nanolib/test_conf/"       \
	"nmq_bridge_cache_test.conf"
#define PKCS11_OLD_CONF_PATH                                    \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH                                         \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_test.conf"
#define PEER_CERT_CONF_PATH                                      \
	"../../../../../nng/src/supplemental/nanolib/test_conf/" \
	"nmq_tls_peer_cert_test.conf"
#else
#define OLD_CONF_PATH \
	"../../../../src/supplemental/nanolib/test_conf/nmq_old_test.conf"
#define CONF_PATH \
	"../../../../src/supplemental/nanolib/test_conf/nmq_test.conf"
#define BRIDGE_CACHE_CONF_PATH \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_bridge_cache_test.conf"
#define PKCS11_OLD_CONF_PATH                               \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_old_test.conf"
#define PKCS11_CONF_PATH                                   \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_tls_pkcs11_test.conf"
#define PEER_CERT_CONF_PATH                                \
	"../../../../src/supplemental/nanolib/test_conf/" \
	"nmq_tls_peer_cert_test.conf"
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

static bool
http_cred_ok(conf_http_server *http, const char *userpass)
{
	return conf_http_server_cred_check(http, userpass, strlen(userpass));
}

void
test_conf_http_server_users(void)
{
	cJSON *users = cJSON_Parse(
	    "[{\"username\":\"admin\",\"password\":\"public\"},"
	    "{\"username\":\"ops\",\"password\":\"s3cret\"},"
	    "{\"username\":\"admin\",\"password\":\"other\"},"
	    "{\"username\":\"nopass\"},"
	    "{\"username\":\"\",\"password\":\"x\"},"
	    "{\"username\":\"a:b\",\"password\":\"x\"}]");
	NUTS_TRUE(cJSON_IsArray(users));

	conf_http_server http;
	conf_http_server_init(&http, 8081);

	size_t           count  = 0;
	conf_http_user **parsed = conf_http_users_parse(users, &count);
	NUTS_TRUE(parsed != NULL);
	NUTS_TRUE(count == 2);
	NUTS_TRUE(strcmp(parsed[0]->username, "admin") == 0);
	NUTS_TRUE(strcmp(parsed[0]->password, "public") == 0);
	NUTS_TRUE(strcmp(parsed[1]->username, "ops") == 0);

	conf_http_server_set_users(&http, parsed, count);
	NUTS_TRUE(http.users_present);
	NUTS_TRUE(http.users_count == 2);
	NUTS_TRUE(http_cred_ok(&http, "admin:public"));
	NUTS_TRUE(http_cred_ok(&http, "ops:s3cret"));
	NUTS_TRUE(!http_cred_ok(&http, "admin:other"));
	NUTS_TRUE(!http_cred_ok(&http, "admin:wrong"));
	NUTS_TRUE(!http_cred_ok(&http, "admin:public:extra"));

	// The legacy pair joins the union.
	conf_http_server_update_legacy(&http, "legacy", "pass");
	NUTS_TRUE(http_cred_ok(&http, "legacy:pass"));
	NUTS_TRUE(http_cred_ok(&http, "admin:public"));

	// Present but empty: no valid user entry, the legacy pair still works.
	cJSON           *bad       = cJSON_Parse("[{\"username\":\"\"},{}]");
	size_t           bad_count = 0;
	conf_http_user **bad_users =
	    conf_http_users_parse(bad, &bad_count);
	NUTS_TRUE(bad_count == 0);
	conf_http_server_set_users(&http, bad_users, bad_count);
	NUTS_TRUE(http.users_present);
	NUTS_TRUE(http.users_count == 0);
	NUTS_TRUE(!http_cred_ok(&http, "admin:public"));
	NUTS_TRUE(http_cred_ok(&http, "legacy:pass"));

	// clear_users() drops the table and the presence flag, leaving the
	// legacy pair in charge again.
	conf_http_server_clear_users(&http);
	NUTS_TRUE(!http.users_present);
	NUTS_TRUE(http.users_count == 0);
	NUTS_TRUE(!http_cred_ok(&http, "admin:public"));
	NUTS_TRUE(http_cred_ok(&http, "legacy:pass"));

	// replace_legacy() replaces the pair as a whole; NULL clears a field.
	conf_http_server_replace_legacy(&http, "onlyuser", NULL);
	NUTS_TRUE(http.username != NULL);
	NUTS_TRUE(strcmp(http.username, "onlyuser") == 0);
	NUTS_TRUE(http.password == NULL);
	NUTS_TRUE(!http_cred_ok(&http, "onlyuser:"));
	conf_http_server_replace_legacy(&http, "u2", "p2");
	NUTS_TRUE(http_cred_ok(&http, "u2:p2"));
	NUTS_TRUE(!http_cred_ok(&http, "legacy:pass"));

	// A non-array value counts as present with no valid entry.
	cJSON           *not_array = cJSON_Parse("\"admin:public\"");
	size_t           na_count  = 0;
	conf_http_user **na =
	    conf_http_users_parse(not_array, &na_count);
	NUTS_TRUE(na == NULL);
	NUTS_TRUE(na_count == 0);

	cJSON_Delete(users);
	cJSON_Delete(bad);
	cJSON_Delete(not_array);
	conf_http_server_destroy(&http);
}

void
test_conf_http_server_users_from_file(void)
{
	conf *conf = get_test_conf(CONF_PATH);
	NUTS_TRUE(conf != NULL);
	if (conf == NULL) {
		return;
	}
	conf_parse_ver2(conf, false);
	NUTS_TRUE(conf->http_server.users_present);
	NUTS_TRUE(conf->http_server.users_count == 2);
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

/** Verifies that the legacy configuration preserves PKCS#11 TLS URIs. */
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

/** Verifies that version 2 configuration preserves PKCS#11 TLS URIs. */
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

/** Verifies peer_cert_as_username per named listener, case-insensitively. */
void
test_conf_parse_ver2_tls_peer_cert_as_username(void)
{
	conf *conf = get_test_conf(PEER_CERT_CONF_PATH);
	NUTS_TRUE(conf != NULL);
	conf_parse_ver2(conf, false);

	NUTS_TRUE(conf->tls_list.count == 3);
	for (size_t i = 0; i < conf->tls_list.count; i++) {
		conf_tls *node = conf->tls_list.nodes[i];
		uint8_t   want = PEER_CERT_AS_USERNAME_DISABLE;
		if (strcmp(node->name, "by_cn") == 0) {
			want = PEER_CERT_AS_USERNAME_CN;
		} else if (strcmp(node->name, "by_dn") == 0) {
			want = PEER_CERT_AS_USERNAME_DN;
		}
		NUTS_TRUE(node->peer_cert_as_username == want);
	}

	conf_fini(conf);
}

/** Verifies case-insensitive PKCS#11 URI detection and rejection cases. */
void
test_conf_tls_is_pkcs11_uri(void)
{
	NUTS_TRUE(conf_tls_is_pkcs11_uri(PKCS11_KEY_URI));
	NUTS_TRUE(conf_tls_is_pkcs11_uri(
	    "PKCS11:token=NanoMQ;object=broker-key;type=private"));
	NUTS_TRUE(!conf_tls_is_pkcs11_uri("/etc/certs/key.pem"));
	NUTS_TRUE(!conf_tls_is_pkcs11_uri(NULL));
}

static conf_bridge_node *
find_bridge_node(conf *conf, const char *name)
{
	for (size_t i = 0; i < conf->bridge.count; i++) {
		conf_bridge_node *node = conf->bridge.nodes[i];
		if (node->name != NULL && strcmp(node->name, name) == 0) {
			return node;
		}
	}
	return NULL;
}

/** Per-Bridge-Node Bridge Cache resolution (design doc §3, ADR 0001/0002). */
void
test_conf_bridge_cache_per_node(void)
{
	conf *conf = get_test_conf(BRIDGE_CACHE_CONF_PATH);
	NUTS_TRUE(conf != NULL);
	if (conf == NULL) {
		return;
	}
	conf_parse_ver2(conf, false);

	// The defaults block is stored on the conf_bridge itself.
	NUTS_TRUE(conf->bridge.sqlite.enable == true);
	NUTS_TRUE(conf->bridge.sqlite.disk_cache_size == 4096);
	NUTS_TRUE(conf->bridge.sqlite.flush_mem_threshold == 7);
	NUTS_TRUE(conf->bridge.sqlite.mounted_file_path != NULL);
	NUTS_TRUE(strcmp(conf->bridge.sqlite.mounted_file_path,
	             "/tmp/nmq_bridge_cache_defaults/") == 0);

	// A node without a cache block inherits the defaults verbatim.
	conf_bridge_node *inherits = find_bridge_node(conf, "inherits");
	NUTS_TRUE(inherits != NULL);
	NUTS_TRUE(inherits->sqlite.enable == true);
	NUTS_TRUE(inherits->sqlite.disk_cache_size == 4096);
	NUTS_TRUE(inherits->sqlite.flush_mem_threshold == 7);
	NUTS_TRUE(inherits->sqlite.mounted_file_path != NULL);
	NUTS_TRUE(strcmp(inherits->sqlite.mounted_file_path,
	             "/tmp/nmq_bridge_cache_defaults/") == 0);

	// A node-level block replaces the defaults wholesale: the fields it does
	// not set fall back to the type defaults, not to the block above.
	conf_bridge_node *overrides = find_bridge_node(conf, "overrides");
	NUTS_TRUE(overrides != NULL);
	NUTS_TRUE(overrides->sqlite.enable == true);
	NUTS_TRUE(overrides->sqlite.disk_cache_size == 32);
	NUTS_TRUE(overrides->sqlite.flush_mem_threshold == 100);
	NUTS_TRUE(overrides->sqlite.mounted_file_path != NULL);
	NUTS_TRUE(strcmp(overrides->sqlite.mounted_file_path,
	             "/tmp/nmq_bridge_cache_overrides/") == 0);

	// The two nodes must not share storage any more.
	NUTS_TRUE(inherits->sqlite.mounted_file_path !=
	    overrides->sqlite.mounted_file_path);

	// Presence implies enable, but an explicit false wins.
	conf_bridge_node *disabled = find_bridge_node(conf, "disabled");
	NUTS_TRUE(disabled != NULL);
	NUTS_TRUE(disabled->sqlite.enable == false);

	// A node-level `sqlite { }` is not an alias: the node inherits the defaults.
	conf_bridge_node *noalias = find_bridge_node(conf, "noalias");
	NUTS_TRUE(noalias != NULL);
	NUTS_TRUE(noalias->sqlite.enable == true);
	NUTS_TRUE(noalias->sqlite.disk_cache_size == 4096);

	conf_fini(conf);
}

/** Per-Bridge-Node Bridge Cache file naming and collision rejection (§4, §12). */
void
test_bridge_cache_file_name(void)
{
	char buf[128];
	char long_name[200];

	NUTS_TRUE(bridge_cache_db_name("mqtt_client", "hub", buf, sizeof(buf)) > 0);
	NUTS_TRUE(strcmp(buf, "mqtt_client_hub.db") == 0);

	NUTS_TRUE(
	    bridge_cache_db_name("mqtt_quic_client", "hub", buf, sizeof(buf)) > 0);
	NUTS_TRUE(strcmp(buf, "mqtt_quic_client_hub.db") == 0);

	// Path-hostile and spacing characters are replaced with '_'.
	NUTS_TRUE(bridge_cache_db_name("mqtt_client", "a/b", buf, sizeof(buf)) > 0);
	NUTS_TRUE(strcmp(buf, "mqtt_client_a_b.db") == 0);
	NUTS_TRUE(
	    bridge_cache_db_name("mqtt_client", "../x y", buf, sizeof(buf)) > 0);
	NUTS_TRUE(strcmp(buf, "mqtt_client_.._x_y.db") == 0);

	// Nothing usable, or no room for the result.
	NUTS_FAIL(bridge_cache_db_name("mqtt_client", "///", buf, sizeof(buf)), -1);
	NUTS_FAIL(bridge_cache_db_name(NULL, "hub", buf, sizeof(buf)), -1);
	NUTS_FAIL(bridge_cache_db_name("mqtt_client", "hub", buf, 4), -1);

	// Long names are truncated to BRIDGE_CACHE_NAME_MAX characters.
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = '\0';
	NUTS_TRUE(
	    bridge_cache_db_name("mqtt_client", long_name, buf, sizeof(buf)) > 0);
	NUTS_TRUE(strlen(buf) == strlen("mqtt_client_") +
	        BRIDGE_CACHE_NAME_MAX + strlen(".db"));

	// Two names that sanitise to the same token must be rejected.
	conf_bridge_node  n1 = { 0 }, n2 = { 0 };
	conf_bridge_node *nodes[2] = { &n1, &n2 };
	conf_bridge      b         = { 0 };
	b.nodes                    = nodes;
	b.count                    = 2;

	n1.name = "a/b";
	n2.name = "a_b";
	NUTS_FAIL(conf_bridge_cache_validate(&b), -1);

	n1.name = "hub";
	n2.name = "cloud";
	NUTS_PASS(conf_bridge_cache_validate(&b));

	n1.name = "///";
	NUTS_FAIL(conf_bridge_cache_validate(&b), -1);

	// Case-insensitive filesystems must not put two bridges on one file.
	n1.name = "Hub";
	n2.name = "hub";
	NUTS_FAIL(conf_bridge_cache_validate(&b), -1);
}

NUTS_TESTS = {
   {"get size", test_get_size},
   {"get time", test_get_time},
   {"conf parse v2", test_conf_parse_ver2},
   {"conf http server users", test_conf_http_server_users},
   {"conf http server users in file", test_conf_http_server_users_from_file},
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
   {"conf parse v2 peer_cert_as_username",
       test_conf_parse_ver2_tls_peer_cert_as_username},
   {"bridge cache per node", test_conf_bridge_cache_per_node},
   {"bridge cache file name", test_bridge_cache_file_name},
   {NULL, NULL}
};
