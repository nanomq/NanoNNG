#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

int
nano_dir_create(const char *fpath)
{
	return nano_mkdir(fpath, 0777);
}

/**/
// int file_trunc_to_zero(const char *fpath)
// {
// 	int fd;

// 	debug_msg("fpath = %s\n", fpath);

// 	fd = open(fpath, O_WRONLY | O_CREAT | O_TRUNC, MODE);
// 	if (fd >= 0)
// 		close(fd);

// 	return 0;
// }

// /*return 1 if exists*/
// int file_exists(const char *fpath)
// {
// 	struct stat st;
// 	int ret;

// 	ret = stat(fpath, &st) == 0 ? 1 : 0;
// 	debug_msg("%s: %i", fpath, ret);

// 	return ret;
// }

// int file_is_symlink(const char *fpath)
// {
// 	int ret;
// 	struct stat st;

// 	ret = lstat(fpath, &st);
// 	if (ret != 0)
// 		return 0;

// 	return S_ISLNK(st.st_mode);
// }

// int file_size(const char *fpath)
// {
// 	int ret;
// 	struct stat st;

// 	if (!file_exists(fpath))
// 		return 0;

// 	ret = stat(fpath, &st);
// 	if (ret != 0)
// 		return -1;

// 	return st.st_size;
// }

// int file_create_symlink(const char *file_path, const char *link_path)
// {
// 	debug_msg("%s => %s", file_path, link_path);
// 	return symlink(file_path, link_path);
// }

// int file_read_int(const char *fpath_fmt, ...)
// {
// 	va_list args;
// 	char buff[10];
// 	int fd, ret = 0;

// 	va_start(args, fpath_fmt);
// 	vsnprintf(fpath_tmp, sizeof(fpath_tmp), fpath_fmt, args);
// 	va_end(args);

// 	if (!file_exists(fpath_tmp))
// 		goto out;

// 	fd = open(fpath_tmp, O_RDONLY);
// 	if (fd < 0)
// 		goto out;

// 	ret = read(fd, buff, sizeof(buff));
// 	if (ret < 0)
// 		goto close;

// 	ret = strtol(buff, (char **)NULL, 10);
// 	debug_msg("fpath = %s, pid = %d", fpath_tmp, ret);

// close:
// 	close(fd);
// out:
// 	return ret;
// }

// int file_read_string(const char *fpath, char *buff, int buff_len)
// {
// 	int fd, ret = 0;

// 	if (!file_exists(fpath))
// 		goto out;

// 	fd = open(fpath, O_RDONLY);
// 	if (fd < 0)
// 		goto out;

// 	ret = read(fd, buff, buff_len);
// 	if (ret < 0)
// 		goto close;

// 	if (ret > 0)
// 		buff[ret - 1] = '\0';

// 	debug_msg("fpath = %s, string = %s", fpath, buff);

// close:
// 	close(fd);
// out:
// 	return ret;
// }

// int file_delete(const char *fpath)
// {
// 	if (!fpath)
// 		return 0;

// 	debug_msg("%s", fpath);
// 	return unlink(fpath);
// }
/**
 * Create dir according to the path input
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