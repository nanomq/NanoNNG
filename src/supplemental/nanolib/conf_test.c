#include "nng/supplemental/nanolib/conf.h"

#include "nuts.h"

void
test_get_size(void)
{
	char    *str_size1 = "3KB";
	char    *str_size2 = "2MB";
	char    *str_size3 = "1GB";
	char    *str_size_tb = "1TB";
	char    *str_size_0  = "s0";
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

TEST_LIST = {
   {"get size", test_get_size},
   {"get time", test_get_time},
   {NULL, NULL} 
};