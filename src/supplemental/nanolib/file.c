#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "core/nng_impl.h"
#include "nng/supplemental/nanolib/file.h"

#ifdef SUPP_PARQUET
#include "supplemental/aes/aes.h"
static char *file_load_aes_key = NULL;
#endif

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

#ifdef SUPP_PARQUET

void
file_load_set_aes_key(const char *aeskey)
{
	file_load_aes_key = (char *)aeskey;
}

size_t
file_load_aes_decrypt(const char *filepath, void **data)
{
	int   len;
	int   plainsz;
	char *plain = NULL;
	char *cipher = NULL;

	len = file_load_data(filepath, (void **)&cipher);
	if (len == 0 || cipher == NULL)
		return 0;

	if (file_load_aes_key == NULL) {
		log_error("no aes key was set when decrypt file %s", filepath);
		nng_free(cipher, 0);
		return 0;
	}
	char *aeskey = file_load_aes_key;

	char tag[32];
	memcpy(tag, cipher, 32);
	plain = nni_aes_gcm_decrypt(cipher, len - 1, aeskey, tag, &plainsz);
	nng_free(cipher, 0);

	if (!plain || plainsz == 0) {
		log_error("AES decrypt %s len %d failed!", filepath, len);
		return 0;
	} else {
		log_info("AES decrypt %s successfully!", filepath);
		*data = plain;
	}
	return plainsz;
}

#endif

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

#ifndef NNG_PLATFORM_WINDOWS
	ret = mkdir(fpath, 0777);
#else
	ret = mkdir(fpath);
#endif
out:
	free(fpath_edit);

	return ret;
}
