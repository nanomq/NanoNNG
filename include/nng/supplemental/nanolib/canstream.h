#ifndef CANSTREAM_H
#define CANSTREAM_H
#include "nng/nng.h"

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static void memcpy_big_endian(void *dest, void *src, int size)
{
	const uint8_t *src_ptr = (const uint8_t *)src;
	uint8_t *dest_ptr = (uint8_t *)dest;
	for (int i = 0; i < size; i++)
	{
		dest_ptr[size - i - 1] = src_ptr[i];
	}
}
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define memcpy_big_endian(dest, src, size) memcpy(dest, src, size)
#else
#error "Unsupported byte order"
#endif

struct canStreamCol {
	uint32_t canid;
	uint8_t busid;
	uint16_t payloadLen;
	void *payload;
};

struct canStreamRow {
	uint64_t timestamp;
	uint32_t colLen;
	struct canStreamCol *col;
};

struct canStream {
	uint32_t rowLen;
	struct canStreamRow *row;
};

int canStreamRowAddColumn(struct canStreamRow *row,
						  uint64_t canid,
						  uint8_t busid,
						  uint16_t payloadLen,
						  void *payload);

int canStreamAddRow(struct canStream *stream, uint64_t timestamp);
void freeCanStream(struct canStream *stream);
struct canStream *parseCanStream(nng_msg **smsg, size_t len);

/* for debug */
void printCanStream(struct canStream *stream);

#endif
