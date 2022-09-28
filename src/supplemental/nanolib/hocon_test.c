#include "core/nng_impl.h"
#include "nng/nng.h"
#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "string.h"
#include <nuts.h>

void
test_hocon_str_to_json(void)
{
	char *test = "bridge.sqlite {\n"
	             "	enable=false\n"
	             "	# enable=false\n"
	             "	// enable=false\n"
	             "	enable=false\n"
	             "	disk_cache_size=102400\n"
	             "	mounted_file_path=\"/tmp/\"\n"
	             "	flush_mem_threshold=100\n"
	             "	resend_interval=5000\n"
	             "}\n"
	             "bridge.sqlite {\n"
	             "	test=false\n"
	             "	test=false\n"
	             "	disk_cache_size=102400\n"
	             "	mounted_file_path=\"/tmp/\"\n"
	             "	flush_mem_threshold=100\n"
	             "	resend_interval=5000\n"
	             "}\n";

	char result[] = " {"
	                "   \"bridge\" :{"
	                "       \"sqlite\": {"
	                "         \"enable\": false,"
	                "         \"test\": false,"
	                "         \"disk_cache_size\": 102400,"
	                "         \"mounted_file_path\": \"/tmp/\","
	                "         \"flush_mem_threshold\": 100,"
	                "         \"resend_interval\": 5000"
	                "       }"
	                "   }"
	                " }";

	cJSON *jso_res = cJSON_Parse(result);
	cJSON *jso     = hocon_str_to_json(test);

	char *str = cJSON_PrintUnformatted(jso);
	puts("\n");
	puts(str);
	puts("\n");

	char *str_res = cJSON_PrintUnformatted(jso_res);
	puts("\n");
	puts(str_res);
	puts("\n");
	cJSON_Delete(jso);
	// cJSON_Delete(jso_res);

	NUTS_TRUE(strlen(str) == strlen(str_res));
	NUTS_PASS(strncmp(str, str_res, strlen(str)));
	free(str);
	free(str_res);
}

// char *
// json_buffer_from_fd(int fd)
// {
// 	char   *buffer    = NULL;
// 	char    buf[8192] = { 0 };
// 	ssize_t ret;
// 
// 	while ((ret = read(fd, buf, sizeof(buf))) > 0) {
// 		for (size_t i = 0; i < sizeof(buf); i++) {
// 			cvector_push_back(buffer, buf[i]);
// 		}
// 		memset(buf, 0, 8192);
// 	}
// 
// 	return buffer;
// }
// 
// void
// test_hocon_str_to_json_read_file(void)
// {
// 	char filename[] = "nanomq_test.conf";
// 
// 	int d = open(filename, O_RDONLY);
// 	if (d < 0) {
// 		fprintf(stderr, "FAIL: unable to open %s: %s\n", filename,
// 		    strerror(errno));
// 		exit(EXIT_FAILURE);
// 	}
// 	char *str = json_buffer_from_fd(d);
// 	puts("\n");
// 	puts(str);
// 	if (str != NULL) {
// 
// 		cJSON *jso = hocon_str_to_json(str);
// 
// 		char *ret = cJSON_PrintUnformatted(jso);
// 		puts("\n");
// 		puts(ret);
// 		puts("\n");
// 
// 		cJSON_free(ret);
// 		cJSON_Delete(jso);
// 		cvector_free(str);
// 
// 	} else {
// 		fprintf(stderr, "FAIL: unable to parse contents of\n");
// 	}
// 	close(d);
// }

NUTS_TESTS = {
	// { "hocon string to json", test_hocon_str_to_json },
	// { "hocon string to json read file", test_hocon_str_to_json_read_file },
	{ NULL, NULL },
};
