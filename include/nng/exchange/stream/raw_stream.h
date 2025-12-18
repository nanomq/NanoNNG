#ifndef RAW_STREAM_H
#define RAW_STREAM_H

#include "nng/exchange/stream/stream.h"
#define RAW_STREAM_NAME "raw"
#define RAW_STREAM_ID 0

int raw_stream_register();
void *raw_cmd_parser(void *);
void *raw_decode(void *);
void *raw_encode(void *);

#endif // RAW_STREAM_H
