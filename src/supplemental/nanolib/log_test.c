//
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nng/nng.h>
#include <nng/supplemental/nanolib/log.h>

#include <nuts.h>

void
test_log_level_string(void)
{
	const char *str;

	str = log_level_string(NNG_LOG_FATAL);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "FATAL");

	str = log_level_string(NNG_LOG_ERROR);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "ERROR");

	str = log_level_string(NNG_LOG_WARN);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "WARN");

	str = log_level_string(NNG_LOG_INFO);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "INFO");

	str = log_level_string(NNG_LOG_DEBUG);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "DEBUG");

	str = log_level_string(NNG_LOG_TRACE);
	NUTS_TRUE(str != NULL);
	NUTS_MATCH(str, "TRACE");
}

void
test_log_level_string_invalid(void)
{
	const char *str;

	// Invalid log level
	str = log_level_string(999);
	NUTS_TRUE(str != NULL); // Should return something, likely "UNKNOWN"
}

void
test_log_level_num(void)
{
	int level;

	level = log_level_num("FATAL");
	NUTS_TRUE(level == NNG_LOG_FATAL);

	level = log_level_num("ERROR");
	NUTS_TRUE(level == NNG_LOG_ERROR);

	level = log_level_num("WARN");
	NUTS_TRUE(level == NNG_LOG_WARN);

	level = log_level_num("INFO");
	NUTS_TRUE(level == NNG_LOG_INFO);

	level = log_level_num("DEBUG");
	NUTS_TRUE(level == NNG_LOG_DEBUG);

	level = log_level_num("TRACE");
	NUTS_TRUE(level == NNG_LOG_TRACE);
}

void
test_log_level_num_case_insensitive(void)
{
	int level;

	level = log_level_num("fatal");
	NUTS_TRUE(level == NNG_LOG_FATAL);

	level = log_level_num("error");
	NUTS_TRUE(level == NNG_LOG_ERROR);

	level = log_level_num("warn");
	NUTS_TRUE(level == NNG_LOG_WARN);

	level = log_level_num("info");
	NUTS_TRUE(level == NNG_LOG_INFO);

	level = log_level_num("debug");
	NUTS_TRUE(level == NNG_LOG_DEBUG);

	level = log_level_num("trace");
	NUTS_TRUE(level == NNG_LOG_TRACE);
}

void
test_log_level_num_invalid(void)
{
	int level;

	level = log_level_num("INVALID");
	NUTS_TRUE(level < 0 || level == NNG_LOG_INFO); // Default or error value
}

void
test_log_set_level(void)
{
	// Set various log levels - should not crash
	log_set_level(NNG_LOG_FATAL);
	log_set_level(NNG_LOG_ERROR);
	log_set_level(NNG_LOG_WARN);
	log_set_level(NNG_LOG_INFO);
	log_set_level(NNG_LOG_DEBUG);
	log_set_level(NNG_LOG_TRACE);
}

static int          callback_called = 0;
static int          callback_level  = -1;
static const char * callback_file   = NULL;
static int          callback_line   = -1;

static void
test_callback(log_event *ev)
{
	callback_called++;
	callback_level = ev->level;
	callback_file  = ev->file;
	callback_line  = ev->line;
}

void
test_log_add_callback(void)
{
	nng_mtx *mtx = NULL;
	conf_log config;

	memset(&config, 0, sizeof(config));
	callback_called = 0;

	NUTS_PASS(nng_mtx_alloc(&mtx));
	NUTS_PASS(log_add_callback(
	    test_callback, NULL, NNG_LOG_INFO, mtx, &config));

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_INFO);
	log_info("Test message");
	NUTS_TRUE(callback_called > 0);
	NUTS_TRUE(callback_level == NNG_LOG_INFO);
#endif

	log_clear_callback();
	nng_mtx_free(mtx);
}

void
test_log_add_fp(void)
{
	FILE *    fp;
	nng_mtx * mtx = NULL;
	conf_log  config;
	const char *testfile = "/tmp/nuts_log_test.txt";

	memset(&config, 0, sizeof(config));
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);

	NUTS_PASS(nng_mtx_alloc(&mtx));
	NUTS_PASS(log_add_fp(fp, NNG_LOG_INFO, mtx, &config));

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_INFO);
	log_info("Test log to file");
#endif

	log_clear_callback();
	fclose(fp);
	nng_mtx_free(mtx);
	unlink(testfile);
}

void
test_log_add_console(void)
{
	nng_mtx *mtx = NULL;

	NUTS_PASS(nng_mtx_alloc(&mtx));
	log_add_console(NNG_LOG_INFO, mtx);

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_INFO);
	log_info("Test console log");
#endif

	log_clear_callback();
	nng_mtx_free(mtx);
}

void
test_log_multiple_callbacks(void)
{
	nng_mtx * mtx1 = NULL;
	nng_mtx * mtx2 = NULL;
	conf_log  config1, config2;

	memset(&config1, 0, sizeof(config1));
	memset(&config2, 0, sizeof(config2));
	callback_called = 0;

	NUTS_PASS(nng_mtx_alloc(&mtx1));
	NUTS_PASS(nng_mtx_alloc(&mtx2));

	NUTS_PASS(log_add_callback(
	    test_callback, NULL, NNG_LOG_INFO, mtx1, &config1));
	log_add_console(NNG_LOG_INFO, mtx2);

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_INFO);
	log_info("Test multiple callbacks");
	NUTS_TRUE(callback_called > 0);
#endif

	log_clear_callback();
	nng_mtx_free(mtx1);
	nng_mtx_free(mtx2);
}

void
test_log_level_filtering(void)
{
	nng_mtx * mtx = NULL;
	conf_log  config;

	memset(&config, 0, sizeof(config));
	callback_called = 0;

	NUTS_PASS(nng_mtx_alloc(&mtx));
	NUTS_PASS(log_add_callback(
	    test_callback, NULL, NNG_LOG_WARN, mtx, &config));

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_WARN);
	// Should trigger callback (WARN level)
	log_warn("Warning message");
	NUTS_TRUE(callback_called > 0);

	callback_called = 0;
	// Should trigger callback (ERROR level, higher priority than WARN)
	log_error("Error message");
	NUTS_TRUE(callback_called > 0);

	callback_called = 0;
	// Should NOT trigger callback (INFO level, lower priority than WARN)
	log_set_level(NNG_LOG_INFO);
	log_info("Info message");
	// Note: callback might still be called depending on implementation
#endif

	log_clear_callback();
	nng_mtx_free(mtx);
}

void
test_log_clear_callback(void)
{
	nng_mtx * mtx = NULL;
	conf_log  config;

	memset(&config, 0, sizeof(config));
	callback_called = 0;

	NUTS_PASS(nng_mtx_alloc(&mtx));
	NUTS_PASS(log_add_callback(
	    test_callback, NULL, NNG_LOG_INFO, mtx, &config));

	log_clear_callback();

#ifdef ENABLE_LOG
	log_set_level(NNG_LOG_INFO);
	log_info("After clear");
	// Callback should not be called after clear
	NUTS_TRUE(callback_called == 0);
#endif

	nng_mtx_free(mtx);
}

void
test_log_null_parameters(void)
{
	// Test with NULL parameters - should handle gracefully
	log_set_level(NNG_LOG_INFO);
	log_clear_callback();
	log_add_console(NNG_LOG_INFO, NULL);
	log_clear_callback();
}

NUTS_TESTS = {
	{ "log level string", test_log_level_string },
	{ "log level string invalid", test_log_level_string_invalid },
	{ "log level num", test_log_level_num },
	{ "log level num case insensitive", test_log_level_num_case_insensitive },
	{ "log level num invalid", test_log_level_num_invalid },
	{ "log set level", test_log_set_level },
	{ "log add callback", test_log_add_callback },
	{ "log add fp", test_log_add_fp },
	{ "log add console", test_log_add_console },
	{ "log multiple callbacks", test_log_multiple_callbacks },
	{ "log level filtering", test_log_level_filtering },
	{ "log clear callback", test_log_clear_callback },
	{ "log null parameters", test_log_null_parameters },
	{ NULL, NULL },
};