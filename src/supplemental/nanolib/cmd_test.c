#include "nng/supplemental/nanolib/cmd.h"

#include "nuts.h"

void
test_nano_cmd(void)
{
	char *cmd_null = NULL;
	NUTS_FAIL(nano_cmd_run(cmd_null), -1);

	char *cmd_invalid = "just test";
	NUTS_FAIL(nano_cmd_frun(cmd_invalid), -1);

	char *cmd = "whoami";
	NUTS_PASS(nano_cmd_frun(cmd));

	nano_cmd_cleanup();
}

NUTS_TESTS = {
   {"nano cmd", test_nano_cmd},
   {NULL, NULL} 
};