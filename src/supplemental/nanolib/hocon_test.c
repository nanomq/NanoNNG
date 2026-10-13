#include <stdio.h>
#include <string.h>

#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/cJSON.h"
#include "nng/supplemental/nanolib/hocon.h"

#include "nuts.h"

static cJSON *
parse_hocon(const char *text)
{
	return hocon_parse_str((char *) text, strlen(text));
}

void
test_hocon_empty_array(void)
{
	cJSON *jso = parse_hocon("log {\n  to = []\n  level = info\n}\n");
	cJSON *log;
	cJSON *to;
	cJSON *level;

	NUTS_TRUE(jso != NULL);
	log   = cJSON_GetObjectItem(jso, "log");
	to    = cJSON_GetObjectItem(log, "to");
	level = cJSON_GetObjectItem(log, "level");
	NUTS_TRUE(cJSON_IsObject(log));
	NUTS_TRUE(cJSON_IsArray(to));
	NUTS_TRUE(cJSON_GetArraySize(to) == 0);
	NUTS_TRUE(cJSON_IsString(level));
	NUTS_MATCH(cJSON_GetStringValue(level), "info");
	cJSON_Delete(jso);
}

void
test_hocon_nonempty_array(void)
{
	cJSON *jso = parse_hocon("a = [file, console]\n");
	cJSON *arr = cJSON_GetObjectItem(jso, "a");

	NUTS_TRUE(jso != NULL);
	NUTS_TRUE(cJSON_IsArray(arr));
	NUTS_TRUE(cJSON_GetArraySize(arr) == 2);
	NUTS_MATCH(cJSON_GetStringValue(cJSON_GetArrayItem(arr, 0)), "file");
	NUTS_MATCH(cJSON_GetStringValue(cJSON_GetArrayItem(arr, 1)), "console");
	cJSON_Delete(jso);
}

void
test_hocon_nested_empty_array(void)
{
	cJSON *jso  = parse_hocon("a = [[]]\nb = [ ]\n");
	cJSON *arr  = cJSON_GetObjectItem(jso, "a");
	cJSON *arr2 = cJSON_GetObjectItem(jso, "b");
	cJSON *inner;

	NUTS_TRUE(jso != NULL);
	NUTS_TRUE(cJSON_IsArray(arr));
	NUTS_TRUE(cJSON_GetArraySize(arr) == 1);
	inner = cJSON_GetArrayItem(arr, 0);
	NUTS_TRUE(cJSON_IsArray(inner));
	NUTS_TRUE(cJSON_GetArraySize(inner) == 0);
	NUTS_TRUE(cJSON_IsArray(arr2));
	NUTS_TRUE(cJSON_GetArraySize(arr2) == 0);
	cJSON_Delete(jso);
}

void
test_hocon_empty_object(void)
{
	cJSON *jso = parse_hocon("tls {\n  ssl = {}\n}\n");
	cJSON *tls = cJSON_GetObjectItem(jso, "tls");
	cJSON *ssl = cJSON_GetObjectItem(tls, "ssl");

	NUTS_TRUE(jso != NULL);
	NUTS_TRUE(cJSON_IsObject(tls));
	NUTS_TRUE(cJSON_IsObject(ssl));
	NUTS_TRUE(cJSON_GetArraySize(ssl) == 0);
	cJSON_Delete(jso);
}

void
test_hocon_parse_file_empty_array(void)
{
	const char text[] = "log {\n  to = []\n  level = info\n}\n";
	char       name[64];
	char      *tmpdir;
	char      *path;
	cJSON     *jso;
	cJSON     *log;
	cJSON     *to;

	snprintf(name, sizeof(name), "nanomq-hocon-empty-%08x-%08x.conf",
	    nng_random(), nng_random());
	NUTS_ASSERT((tmpdir = nni_plat_temp_dir()) != NULL);
	NUTS_ASSERT((path = nni_file_join(tmpdir, name)) != NULL);
	nng_strfree(tmpdir);
	NUTS_ASSERT(nni_file_put(path, text, strlen(text)) == 0);

	jso = hocon_parse_file(path);
	NUTS_PASS(nni_file_delete(path));
	nng_strfree(path);
	NUTS_ASSERT(jso != NULL);
	log = cJSON_GetObjectItem(jso, "log");
	to  = cJSON_GetObjectItem(log, "to");
	NUTS_TRUE(cJSON_IsArray(to));
	NUTS_TRUE(cJSON_GetArraySize(to) == 0);
	cJSON_Delete(jso);
}

NUTS_TESTS = {
	{ "empty array", test_hocon_empty_array },
	{ "nonempty array", test_hocon_nonempty_array },
	{ "nested empty array", test_hocon_nested_empty_array },
	{ "empty object", test_hocon_empty_object },
	{ "parse file empty array", test_hocon_parse_file_empty_array },
	{ NULL, NULL }
};
