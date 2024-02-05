#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/file.h"

#ifdef NNG_PLATFORM_WINDOWS
#define nano_mkdir(path, mode) mkdir(path)
#else
#define nano_mkdir(path, mode) mkdir(path, mode)
#endif

#ifndef NNG_PLATFORM_WINDOWS

int64_t
nano_getline(char **restrict line, size_t *restrict len, FILE *restrict fp)
{
	return getline(line, len, fp);
}

#else

int64_t
nano_getline(char **restrict line, size_t *restrict len, FILE *restrict fp)
{
	// Check if either line, len or fp are NULL pointers
	if (line == NULL || len == NULL || fp == NULL) {
		errno = EINVAL;
		return -1;
	}

	// Use a chunk array of 128 bytes as parameter for fgets
	char chunk[128];

	// Allocate a block of memory for *line if it is NULL or smaller than
	// the chunk array
	if (*line == NULL || *len < sizeof(chunk)) {
		*len = sizeof(chunk);
		if ((*line = malloc(*len)) == NULL) {
			errno = ENOMEM;
			return -1;
		}
	}

	// "Empty" the string
	(*line)[0] = '\0';

	while (fgets(chunk, sizeof(chunk), fp) != NULL) {
		// Resize the line buffer if necessary
		size_t len_used   = strlen(*line);
		size_t chunk_used = strlen(chunk);

		if (*len - len_used < chunk_used) {
			// Check for overflow
			if (*len > SIZE_MAX / 2) {
				errno = EOVERFLOW;
				return -1;
			} else {
				*len *= 2;
			}

			if ((*line = realloc(*line, *len)) == NULL) {
				errno = ENOMEM;
				return -1;
			}
		}

		// Copy the chunk to the end of the line buffer
		memcpy(*line + len_used, chunk, chunk_used);
		len_used += chunk_used;
		(*line)[len_used] = '\0';

		// Check if *line contains '\n', if yes, return the *line
		// length
		if ((*line)[len_used - 1] == '\n') {
			return len_used;
		}
	}

	return -1;
}

#endif

/*return true if exists*/
bool
nano_file_exists(const char *fpath)
{
	return nni_plat_file_exists(fpath);
}

char *
nano_getcwd(char *buf, size_t size)
{
	return nni_plat_getcwd(buf, size);
}

int
file_write_string(const char *fpath, const char *string)
{
	return nni_plat_file_put(fpath, string, strlen(string));
}

size_t
file_load_data(const char *filepath, void **data)
{
	size_t size;

	if (nni_plat_file_get(filepath, data, &size) != 0) {
		return 0;
	}
	size++;
	uint8_t *buf  = *data;
	buf           = realloc(buf, size);
	buf[size - 1] = '\0';
	*data         = buf;
	return size;
}

char *
nano_concat_path(const char *dir, const char *file_name)
{
	if (file_name == NULL) {
		return NULL;
	}

#if defined(NNG_PLATFORM_WINDOWS)
	char *directory = dir == NULL ? nni_strdup(".\\") : nni_strdup(dir);
#else
	char *directory = dir == NULL ? nni_strdup("./") : nni_strdup(dir);
#endif

	size_t path_len = strlen(directory) + strlen(file_name) + 3;
	char * path     = nng_zalloc(path_len);

#if defined(NNG_PLATFORM_WINDOWS)
	snprintf(path, path_len, "%s%s%s", directory,
	    directory[strlen(directory) - 1] == '\\' ? "" : "\\", file_name);
#else
	snprintf(path, path_len, "%s%s%s", directory,
	    directory[strlen(directory) - 1] == '/' ? "" : "/", file_name);
#endif

	nni_strfree(directory);

	return path;
}

/**
 * Create dir according to the path input, strip file name
 * @fpath : /tmp/log/nanomq.log.1
 * then create dir /tmp/log/, skip nanomq.log.1
*/
int file_create_dir(const char *fpath)
{
	char *last_slash = NULL, *fpath_edit = NULL;
	int ret = -1;

	log_info("dir = %s", fpath);

	if (fpath[strlen(fpath) - 1] != '/') {
		fpath_edit = malloc(strlen(fpath) + 1);
		if (!fpath_edit)
			return -1;

		strncpy(fpath_edit, fpath, strlen(fpath) + 1);
		fpath_edit[strlen(fpath)] = '\0';

		last_slash = strrchr(fpath_edit, '/');

		/* not a single slash in the string ? */
		if (!last_slash)
			goto out;

		*last_slash = '\0';
		fpath = fpath_edit;
	}

	log_info("mkdir = %s", fpath);
	ret = mkdir(fpath, 0777);
out:
	free(fpath_edit);

	return ret;
}
