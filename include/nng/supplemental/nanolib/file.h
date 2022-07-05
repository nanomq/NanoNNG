#ifndef NANO_FILE_H
#define NANO_FILE_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

bool    nano_file_exists(const char *fpath);
int64_t nano_getline(
    char **restrict line, size_t *restrict len, FILE *restrict fp);
int file_write_string(const char *fpath, const char *string);
int file_load_data(const char *filepath, void **data);

#endif
