//
// Copyright 2025 EMQX. All rights reserved.
// Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

// Zephyr file operations.
//
// When CONFIG_FILE_SYSTEM is enabled, file operations are implemented
// using the POSIX API (fopen, stat, mkdir, opendir, etc.) which Zephyr
// maps to its native fs_* layer.  Without CONFIG_FILE_SYSTEM, all
// operations return NNG_ENOTSUP (or 0 / -1 for benign callers).
//
// NanoNNG uses files mainly for TLS certificate storage.  If TLS is
// not needed, file system support can be omitted entirely.

#include "core/nng_impl.h"

#ifdef NNG_PLATFORM_ZEPHYR

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Without file system support, stub everything out.
// ---------------------------------------------------------------------------
#ifndef CONFIG_FILE_SYSTEM

int
nni_plat_make_parent_dirs(const char *path)
{
	NNI_ARG_UNUSED(path);
	// Return success so callers that don't actually need
	// directories (e.g. TLS cert stores) don't break.
	return (0);
}

int
nni_plat_file_put(const char *name, const void *data, size_t len)
{
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(data);
	NNI_ARG_UNUSED(len);
	return (NNG_ENOTSUP);
}

int
nni_plat_file_get(const char *name, void **datap, size_t *lenp)
{
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(datap);
	NNI_ARG_UNUSED(lenp);
	return (NNG_ENOTSUP);
}

int
nni_plat_file_delete(const char *name)
{
	NNI_ARG_UNUSED(name);
	return (NNG_ENOTSUP);
}

int
nni_plat_file_type(const char *name, int *ftype)
{
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(ftype);
	return (NNG_ENOTSUP);
}

bool
nni_plat_file_exists(const char *path)
{
	NNI_ARG_UNUSED(path);
	return (false);
}

int
nni_plat_file_size(const char *path, size_t *sizep)
{
	NNI_ARG_UNUSED(path);
	NNI_ARG_UNUSED(sizep);
	return (NNG_ENOTSUP);
}

int
nni_plat_file_walk(const char *name, nni_plat_file_walker walker, void *arg,
    int flags)
{
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(walker);
	NNI_ARG_UNUSED(arg);
	NNI_ARG_UNUSED(flags);
	return (NNG_ENOTSUP);
}

int
nni_plat_access(const char *name, int flag)
{
	NNI_ARG_UNUSED(name);
	NNI_ARG_UNUSED(flag);
	return (-1);
}

#else // CONFIG_FILE_SYSTEM

// ---------------------------------------------------------------------------
// File system is available — real implementations using POSIX APIs.
// These follow the same patterns as src/platform/posix/posix_file.c.
// ---------------------------------------------------------------------------

int
nni_plat_make_parent_dirs(const char *path)
{
	char *dup;
	char *p;
	int   rv;

	if ((dup = nni_strdup(path)) == NULL) {
		return (NNG_ENOMEM);
	}
	p = dup;
	while ((p = strchr(p, '/')) != NULL) {
		if (p != dup) {
			*p = '\0';
			rv = mkdir(dup, S_IRWXU);
			*p = '/';
			if ((rv != 0) && (errno != EEXIST)) {
				nni_strfree(dup);
				return (nni_plat_errno(errno));
			}
		}
		// collapse grouped "/" characters
		while (*p == '/') {
			p++;
		}
	}
	nni_strfree(dup);
	return (0);
}

int
nni_plat_file_put(const char *name, const void *data, size_t len)
{
	FILE *f;
	int   rv = 0;

	// The name may contain a directory path that does not exist.
	// In that case try to create the entire tree.
	if (strchr(name, '/') != NULL) {
		if ((rv = nni_plat_make_parent_dirs(name)) != 0) {
			return (rv);
		}
	}

	if ((f = fopen(name, "wb")) == NULL) {
		return (nni_plat_errno(errno));
	}
	if (fwrite(data, 1, len, f) != len) {
		rv = nni_plat_errno(errno);
		(void) unlink(name);
	}
	(void) fclose(f);
	return (rv);
}

int
nni_plat_file_get(const char *name, void **datap, size_t *lenp)
{
	FILE *      f;
	struct stat st;
	int         rv = 0;
	size_t      len;
	void *      data;

	if ((f = fopen(name, "rb")) == NULL) {
		return (nni_plat_errno(errno));
	}

	if (stat(name, &st) != 0) {
		rv = nni_plat_errno(errno);
		(void) fclose(f);
		return (rv);
	}

	len = (size_t) st.st_size;
	if (len > 0) {
		if ((data = nni_alloc(len)) == NULL) {
			rv = NNG_ENOMEM;
			goto done;
		}
		if (fread(data, 1, len, f) != len) {
			rv = nni_plat_errno(errno);
			nni_free(data, len);
			goto done;
		}
	} else {
		data = NULL;
	}
	*datap = data;
	*lenp  = len;
done:
	(void) fclose(f);
	return (rv);
}

int
nni_plat_file_delete(const char *name)
{
	if (rmdir(name) == 0) {
		return (0);
	}
	if ((errno == ENOTDIR) && (unlink(name) == 0)) {
		return (0);
	}
	if (errno == ENOENT) {
		return (0);
	}
	return (nni_plat_errno(errno));
}

int
nni_plat_file_type(const char *name, int *typep)
{
	struct stat sbuf;

	if (stat(name, &sbuf) != 0) {
		return (nni_plat_errno(errno));
	}
	switch (sbuf.st_mode & S_IFMT) {
	case S_IFREG:
		*typep = NNI_PLAT_FILE_TYPE_FILE;
		break;
	case S_IFDIR:
		*typep = NNI_PLAT_FILE_TYPE_DIR;
		break;
	default:
		*typep = NNI_PLAT_FILE_TYPE_OTHER;
		break;
	}
	return (0);
}

bool
nni_plat_file_exists(const char *path)
{
	struct stat sbuf;
	return (stat(path, &sbuf) == 0);
}

int
nni_plat_file_size(const char *path, size_t *sizep)
{
	struct stat sbuf;
	if (stat(path, &sbuf) != 0) {
		return (nni_plat_errno(errno));
	}
	*sizep = (size_t) sbuf.st_size;
	return (0);
}

static int
nni_plat_file_walk_inner(const char *name, nni_plat_file_walker walkfn,
    void *arg, int flags, bool *stop)
{
	DIR *dir;

	if ((dir = opendir(name)) == NULL) {
		return (nni_plat_errno(errno));
	}
	for (;;) {
		int            rv;
		struct dirent *ent;
		struct stat    sbuf;
		char *         path;
		int            walkrv;

		if ((ent = readdir(dir)) == NULL) {
			closedir(dir);
			return (0);
		}
		// Skip "." and ".." entries.
		if ((strcmp(ent->d_name, ".") == 0) ||
		    (strcmp(ent->d_name, "..") == 0)) {
			continue;
		}
		if ((rv = nni_asprintf(&path, "%s/%s", name,
		         ent->d_name)) != 0) {
			closedir(dir);
			return (rv);
		}
		if (stat(path, &sbuf) != 0) {
			if (errno == ENOENT) { // deleted while walking
				nni_strfree(path);
				continue;
			}
			rv = nni_plat_errno(errno);
			nni_strfree(path);
			closedir(dir);
			return (rv);
		}
		if (flags & NNI_PLAT_FILE_WALK_FILES_ONLY) {
			if ((sbuf.st_mode & S_IFMT) == S_IFREG) {
				walkrv = walkfn(path, arg);
			} else {
				walkrv = NNI_PLAT_FILE_WALK_CONTINUE;
			}
		} else {
			walkrv = walkfn(path, arg);
		}

		if (walkrv == NNI_PLAT_FILE_WALK_STOP) {
			*stop = true;
		}

		if ((!*stop) && (walkrv != NNI_PLAT_FILE_WALK_PRUNE_CHILD) &&
		    ((flags & NNI_PLAT_FILE_WALK_SHALLOW) == 0) &&
		    ((sbuf.st_mode & S_IFMT) == S_IFDIR)) {
			rv = nni_plat_file_walk_inner(
			    path, walkfn, arg, flags, stop);
			if (rv != 0) {
				nni_strfree(path);
				closedir(dir);
				return (rv);
			}
		}

		nni_strfree(path);

		if ((walkrv == NNI_PLAT_FILE_WALK_PRUNE_SIB) || (*stop)) {
			break;
		}
	}
	closedir(dir);
	return (0);
}

int
nni_plat_file_walk(const char *name, nni_plat_file_walker walkfn, void *arg,
    int flags)
{
	bool stop = false;

	return (nni_plat_file_walk_inner(name, walkfn, arg, flags, &stop));
}

// Zephyr does not provide access() or its mode constants.
// Define them locally and emulate access() via stat().
#ifndef F_OK
#define F_OK 0
#endif
#ifndef R_OK
#define R_OK 4
#endif
#ifndef W_OK
#define W_OK 2
#endif
#ifndef X_OK
#define X_OK 1
#endif

int
nni_plat_access(const char *name, int flag)
{
	struct stat sbuf;

	if (stat(name, &sbuf) != 0) {
		if (errno == ENOENT) {
			return (-1); // file not found
		}
		return (nni_plat_errno(errno));
	}

	switch (flag) {
	case F_OK:
		return (0);
	case R_OK:
		return ((sbuf.st_mode & S_IRUSR) ? 0 : -1);
	case W_OK:
		return ((sbuf.st_mode & S_IWUSR) ? 0 : -1);
	case X_OK:
		return ((sbuf.st_mode & S_IXUSR) ? 0 : -1);
	default:
		return (-1);
	}
}

#endif // CONFIG_FILE_SYSTEM

const char *
nni_plat_file_basename(const char *path)
{
	const char *end;

	if ((end = strrchr(path, '/')) != NULL) {
		return (end + 1);
	}
	return (path);
}

#endif // NNG_PLATFORM_ZEPHYR
