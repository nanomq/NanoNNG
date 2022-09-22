#include "nng/supplemental/nanolib/cvector.h"
#include "nng/supplemental/nanolib/hocon.h"
#include "nng/nng.h"
#include "core/nng_impl.h"
#include "string.h"
#include <nuts.h>



void test_hocon_str_to_json(void)
{
    char *test = 
        "\n bridge.sqlite {"
        "\n 	enable=false,"
        "\n 	# enable=false,"
        "\n 	// enable=false,"
        "\n 	enable=false,"
        "\n 	disk_cache_size=102400,"
        "\n 	mounted_file_path=\"/tmp/\","
        "\n 	flush_mem_threshold=100,"
        "\n 	resend_interval=5000,"
        "\n }"
        "\n bridge.sqlite {"
        "\n 	test=false,"
        "\n 	test=false,"
        "\n 	disk_cache_size=102400,"
        "\n 	mounted_file_path=\"/tmp/\","
        "\n 	flush_mem_threshold=100,"
        "\n 	resend_interval=5000,"
        "\n }\n";


    char result[] = 
        " {"
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
    cJSON *jso = hocon_str_to_json(test);

    char *str = cJSON_PrintUnformatted(jso);
    puts("\n");
    puts(str);
    puts("\n");


    char *str_res = cJSON_PrintUnformatted(jso_res);
    puts("\n");
    puts(str_res);
    puts("\n");
    cJSON_Delete(jso);
    cJSON_Delete(jso_res);

    NUTS_TRUE(strlen(str) == strlen(str_res));
    NUTS_PASS(strncmp(str, str_res, strlen(str)));
    free(str);
    free(str_res);
}



NUTS_TESTS = {
	{ "hocon string to json", test_hocon_str_to_json },
	{ NULL, NULL },
};
