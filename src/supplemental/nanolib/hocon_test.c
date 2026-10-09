#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	char  path[] = "/tmp/nanomq-hocon-empty-XXXXXX";
	int   fd     = mkstemp(path);
	FILE *fp;
	cJSON *jso;
	cJSON *log;
	cJSON *to;

	NUTS_TRUE(fd >= 0);
	fp = fdopen(fd, "w");
	NUTS_TRUE(fp != NULL);
	fputs("log {\n  to = []\n  level = info\n}\n", fp);
	fclose(fp);

	jso = hocon_parse_file(path);
	unlink(path);
	NUTS_TRUE(jso != NULL);
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
