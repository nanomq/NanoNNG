//
// Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2017 Capitar IT Group BV <info@capitar.com>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef CORE_STRS_H
#define CORE_STRS_H

// Safe string functions, in case the platform misses these.

extern char * nni_strdup(const char *);
extern char * nni_strndup(const char *, size_t);
extern char * nni_strnins(char *, const char *, size_t , size_t);
extern char * nni_strncat(char *, const char *, size_t , size_t);
extern void   nni_strfree(char *);
extern size_t nni_strlcpy(char *, const char *, size_t);
extern size_t nni_strnlen(const char *, size_t);
extern char * nni_strcasestr(const char *, const char *);
extern int    nni_strncasecmp(const char *, const char *, size_t);
extern int    nni_strcasecmp(const char *, const char *);
extern int    nni_asprintf(char **, const char *, ...);

#endif // CORE_STRS_H
