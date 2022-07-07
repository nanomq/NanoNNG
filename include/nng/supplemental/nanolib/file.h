#ifndef NANO_FILE_H
#define NANO_FILE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

extern bool    nano_file_exists(const char *fpath);
extern char *  nano_getcwd(char *buf, size_t size);
extern int64_t nano_getline(
    char **restrict line, size_t *restrict len, FILE *restrict fp);
extern int file_write_string(const char *fpath, const char *string);
extern int file_load_data(const char *filepath, void **data);

#endif
