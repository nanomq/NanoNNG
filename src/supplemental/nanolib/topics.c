#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/topics.h"


static int count_slashes(const char* str, size_t len) {
    int count = 0;
    for (size_t i = 0; i < len; ++i) {
        if (str[i] == '/') ++count;
    }
    return count;
}






