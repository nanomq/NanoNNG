#ifndef RAW_STREAM_H
#define RAW_STREAM_H

#include "nng/exchange/stream/stream.h"
#define RAW_STREAM_NAME "raw"
#define RAW_STREAM_ID 0
void *raw_stream_decode(void *data, uint32_t len, uint32_t *outlen);
void *raw_stream_encode(void *data, uint32_t len, uint32_t *outlen);
int raw_stream_register();

#endif // RAW_STREAM_H
