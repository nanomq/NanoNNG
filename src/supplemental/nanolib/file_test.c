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
#include <sys/stat.h>
#include <unistd.h>

#include <nng/nng.h>
#include <nng/supplemental/nanolib/file.h>

#include <nuts.h>

void
test_file_exists(void)
{
	const char *testfile = "/tmp/nuts_file_exists_test.txt";
	FILE *      fp;

	// Create a test file
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);
	fprintf(fp, "test");
	fclose(fp);

	NUTS_TRUE(nano_file_exists(testfile));
	unlink(testfile);
	NUTS_TRUE(!nano_file_exists(testfile));
}

void
test_file_exists_null(void)
{
	NUTS_TRUE(!nano_file_exists(NULL));
}

void
test_nano_getcwd(void)
{
	char   buf[1024];
	char * result;
	char * expected;

	result   = nano_getcwd(buf, sizeof(buf));
	expected = getcwd(NULL, 0);
	NUTS_TRUE(result != NULL);
	NUTS_TRUE(expected != NULL);
	NUTS_MATCH(result, expected);
	free(expected);
}

void
test_nano_getcwd_null_buffer(void)
{
	char *result = nano_getcwd(NULL, 0);
	NUTS_TRUE(result != NULL);
	free(result);
}

void
test_nano_getcwd_small_buffer(void)
{
	char buf[4];
	char *result = nano_getcwd(buf, sizeof(buf));
	// Should fail with small buffer
	NUTS_TRUE(result == NULL);
}

void
test_nano_concat_path(void)
{
	char *result;

	result = nano_concat_path("/tmp", "test.txt");
	NUTS_TRUE(result != NULL);
	NUTS_MATCH(result, "/tmp/test.txt");
	nng_strfree(result);

	result = nano_concat_path("/tmp/", "test.txt");
	NUTS_TRUE(result != NULL);
	NUTS_MATCH(result, "/tmp/test.txt");
	nng_strfree(result);
}

void
test_nano_concat_path_null(void)
{
	char *result;

	result = nano_concat_path(NULL, "test.txt");
	NUTS_TRUE(result == NULL);

	result = nano_concat_path("/tmp", NULL);
	NUTS_TRUE(result == NULL);

	result = nano_concat_path(NULL, NULL);
	NUTS_TRUE(result == NULL);
}

void
test_file_write_string(void)
{
	const char *testfile = "/tmp/nuts_file_write_test.txt";
	const char *content  = "Hello, World!";
	FILE *      fp;
	char        buf[256];

	NUTS_PASS(file_write_string(testfile, content));

	fp = fopen(testfile, "r");
	NUTS_TRUE(fp != NULL);
	NUTS_TRUE(fgets(buf, sizeof(buf), fp) != NULL);
	NUTS_MATCH(buf, content);
	fclose(fp);
	unlink(testfile);
}

void
test_file_write_string_empty(void)
{
	const char *testfile = "/tmp/nuts_file_write_empty_test.txt";
	const char *content  = "";
	FILE *      fp;
	char        buf[256];

	NUTS_PASS(file_write_string(testfile, content));

	fp = fopen(testfile, "r");
	NUTS_TRUE(fp != NULL);
	// Empty file should have no content
	NUTS_TRUE(fgets(buf, sizeof(buf), fp) == NULL);
	fclose(fp);
	unlink(testfile);
}

void
test_file_write_string_overwrite(void)
{
	const char *testfile  = "/tmp/nuts_file_write_overwrite_test.txt";
	const char *content1  = "First content";
	const char *content2  = "Second content";
	FILE *      fp;
	char        buf[256];

	NUTS_PASS(file_write_string(testfile, content1));
	NUTS_PASS(file_write_string(testfile, content2));

	fp = fopen(testfile, "r");
	NUTS_TRUE(fp != NULL);
	NUTS_TRUE(fgets(buf, sizeof(buf), fp) != NULL);
	NUTS_MATCH(buf, content2);
	fclose(fp);
	unlink(testfile);
}

void
test_file_create_dir(void)
{
	const char * testdir = "/tmp/nuts_dir_test";
	struct stat  st;

	// Clean up if exists
	rmdir(testdir);

	NUTS_PASS(file_create_dir(testdir));
	NUTS_TRUE(stat(testdir, &st) == 0);
	NUTS_TRUE(S_ISDIR(st.st_mode));
	rmdir(testdir);
}

void
test_file_create_dir_exists(void)
{
	const char * testdir = "/tmp/nuts_dir_exists_test";
	struct stat  st;

	// Clean up if exists
	rmdir(testdir);

	NUTS_PASS(file_create_dir(testdir));
	// Creating again should succeed (idempotent)
	NUTS_PASS(file_create_dir(testdir));
	NUTS_TRUE(stat(testdir, &st) == 0);
	NUTS_TRUE(S_ISDIR(st.st_mode));
	rmdir(testdir);
}

void
test_file_create_nested_dir(void)
{
	const char * testdir = "/tmp/nuts_nested/subdir";
	struct stat  st;

	// Clean up if exists
	rmdir("/tmp/nuts_nested/subdir");
	rmdir("/tmp/nuts_nested");

	NUTS_PASS(file_create_dir(testdir));
	NUTS_TRUE(stat(testdir, &st) == 0);
	NUTS_TRUE(S_ISDIR(st.st_mode));
	rmdir("/tmp/nuts_nested/subdir");
	rmdir("/tmp/nuts_nested");
}

void
test_file_load_data(void)
{
	const char *testfile = "/tmp/nuts_file_load_test.txt";
	const char *content  = "Test data for loading";
	FILE *      fp;
	void *      data = NULL;
	size_t      size;

	// Create test file
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);
	fprintf(fp, "%s", content);
	fclose(fp);

	size = file_load_data(testfile, &data);
	NUTS_TRUE(size > 0);
	NUTS_TRUE(data != NULL);
	NUTS_TRUE(memcmp(data, content, strlen(content)) == 0);
	free(data);
	unlink(testfile);
}

void
test_file_load_data_nonexistent(void)
{
	const char *testfile = "/tmp/nuts_nonexistent_file.txt";
	void *      data     = NULL;
	size_t      size;

	size = file_load_data(testfile, &data);
	NUTS_TRUE(size == 0);
	NUTS_TRUE(data == NULL);
}

void
test_file_load_data_empty(void)
{
	const char *testfile = "/tmp/nuts_empty_file.txt";
	FILE *      fp;
	void *      data = NULL;
	size_t      size;

	// Create empty file
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);
	fclose(fp);

	size = file_load_data(testfile, &data);
	// Empty file should return 0 size
	NUTS_TRUE(size == 0);
	unlink(testfile);
}

void
test_nano_getline(void)
{
	const char *testfile = "/tmp/nuts_getline_test.txt";
	FILE *      fp;
	char *      line = NULL;
	size_t      len  = 0;
	int64_t     read;

	// Create test file with multiple lines
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);
	fprintf(fp, "First line\nSecond line\nThird line\n");
	fclose(fp);

	// Read lines
	fp = fopen(testfile, "r");
	NUTS_TRUE(fp != NULL);

	read = nano_getline(&line, &len, fp);
	NUTS_TRUE(read > 0);
	NUTS_MATCH(line, "First line\n");

	read = nano_getline(&line, &len, fp);
	NUTS_TRUE(read > 0);
	NUTS_MATCH(line, "Second line\n");

	read = nano_getline(&line, &len, fp);
	NUTS_TRUE(read > 0);
	NUTS_MATCH(line, "Third line\n");

	// EOF
	read = nano_getline(&line, &len, fp);
	NUTS_TRUE(read == -1);

	free(line);
	fclose(fp);
	unlink(testfile);
}

void
test_nano_getline_no_newline(void)
{
	const char *testfile = "/tmp/nuts_getline_no_newline_test.txt";
	FILE *      fp;
	char *      line = NULL;
	size_t      len  = 0;
	int64_t     read;

	// Create test file without trailing newline
	fp = fopen(testfile, "w");
	NUTS_TRUE(fp != NULL);
	fprintf(fp, "Line without newline");
	fclose(fp);

	fp   = fopen(testfile, "r");
	NUTS_TRUE(fp != NULL);

	read = nano_getline(&line, &len, fp);
	NUTS_TRUE(read > 0);
	NUTS_MATCH(line, "Line without newline");

	free(line);
	fclose(fp);
	unlink(testfile);
}

NUTS_TESTS = {
	{ "file exists", test_file_exists },
	{ "file exists null", test_file_exists_null },
	{ "nano getcwd", test_nano_getcwd },
	{ "nano getcwd null buffer", test_nano_getcwd_null_buffer },
	{ "nano getcwd small buffer", test_nano_getcwd_small_buffer },
	{ "nano concat path", test_nano_concat_path },
	{ "nano concat path null", test_nano_concat_path_null },
	{ "file write string", test_file_write_string },
	{ "file write string empty", test_file_write_string_empty },
	{ "file write string overwrite", test_file_write_string_overwrite },
	{ "file create dir", test_file_create_dir },
	{ "file create dir exists", test_file_create_dir_exists },
	{ "file create nested dir", test_file_create_nested_dir },
	{ "file load data", test_file_load_data },
	{ "file load data nonexistent", test_file_load_data_nonexistent },
	{ "file load data empty", test_file_load_data_empty },
	{ "nano getline", test_nano_getline },
	{ "nano getline no newline", test_nano_getline_no_newline },
	{ NULL, NULL },
};