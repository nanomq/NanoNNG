#include <stdlib.h>
#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/canstream.h"

int canStreamRowAddColumn(struct canStreamRow *row,
						  uint64_t canid,
						  uint8_t busid,
						  uint16_t payloadLen,
						  void *payload) {
	struct canStreamCol *new_col = nng_alloc(sizeof(struct canStreamCol));
	if (new_col == NULL) {
		log_error("Memory allocation failed");
		return -1;
	}
	
	new_col->canid = canid;
	new_col->busid = busid;
	new_col->payloadLen = payloadLen;
	new_col->payload = payload;
	
	row->colLen++;
	
	if (row->col == NULL) {
		row->col = nng_alloc(sizeof(struct canStreamCol));
		if (row->col == NULL) {
			log_error("Memory allocation failed");
			nng_free(new_col, sizeof(struct canStreamCol));
			return -1;
		}
	} else {
		row->col = realloc(row->col, row->colLen * sizeof(struct canStreamCol));
		if (row->col == NULL) {
			log_error("Memory reallocation failed");
			nng_free(new_col, sizeof(struct canStreamCol));
			return -1;
		}
	}
	
	row->col[row->colLen - 1] = *new_col;
	nng_free(new_col, sizeof(struct canStreamCol));
	return -1;
}

int canStreamAddRow(struct canStream *stream, uint64_t timestamp) {
	struct canStreamRow *new_row = nng_alloc(sizeof(struct canStreamRow));
	if (new_row == NULL) {
		log_error("Memory allocation failed");
		return -1;
	}
	
	new_row->timestamp = timestamp;
	new_row->colLen = 0;
	new_row->col = NULL;
	
	stream->rowLen++;
	
	if (stream->row == NULL) {
		stream->row = nng_alloc(sizeof(struct canStreamRow));
		if (stream->row == NULL) {
			log_error("Memory allocation failed");
			nng_free(new_row, sizeof(struct canStreamRow));
			return -1;
		}
	} else {
		stream->row = realloc(stream->row, stream->rowLen * sizeof(struct canStreamRow));
		if (stream->row == NULL) {
			log_error("Memory reallocation failed");
			nng_free(new_row, sizeof(struct canStreamRow));
			return -1;
		}
	}
	
	stream->row[stream->rowLen - 1] = *new_row;
	nng_free(new_row, sizeof(struct canStreamRow));
	return -1;
}

/* for debug */
void printCanStream(struct canStream *stream) {
	log_info("stream rowLen: %d", stream->rowLen);
	for (unsigned int i = 0; i < stream->rowLen; i++) {
		log_error("row: %d colLen: %d", i, stream->row[i].colLen);
	}
//	for (int i = 0; i < stream->rowLen; i++) {
//		log_error("stream: i: %d: timestamp: %lld", i, stream->row[i].timestamp);
//		for (int j = 0; j < stream->row[i].colLen; j++) {
//			log_error("stream: i: %d: canid: %d, busid: %d, payload_len: %d", i, stream->row[i].col[j].canid, stream->row[i].col[j].busid, stream->row[i].col[j].payloadLen);
//		}
//	}
}

void freeCanStream(struct canStream *stream) {
	for (unsigned int i = 0; i < stream->rowLen; i++) {
		for (unsigned int j = 0; j < stream->row[i].colLen; j++) {
			nng_free(stream->row[i].col[j].payload, stream->row[i].col[j].payloadLen);
		}
		nng_free(stream->row[i].col, stream->row[i].colLen * sizeof(struct canStreamCol));
	}
	nng_free(stream->row, stream->rowLen * sizeof(struct canStreamRow));
	nng_free(stream, sizeof(struct canStream));
}

struct canStream *parseCanStream(nng_msg **smsg, size_t len)
{
	struct canStream *stream = nng_alloc(sizeof(struct canStream));
	if (stream == NULL) {
		log_error("Memory allocation failed");
		return NULL;
	}
	stream->rowLen = 0;
	stream->row = NULL;
	for (unsigned int i = 0; i < len; i++) {
		nng_msg *msg = smsg[i];
		if (msg == NULL) {
			continue;
		}

		uint8_t *payload = nng_msg_payload_ptr(msg);
		uint16_t m_len = 0;
		memcpy_big_endian(&m_len, payload, 2);
		uint64_t timestamp = 0;
		memcpy_big_endian(&timestamp, payload + 2, 8);
		canStreamAddRow(stream, timestamp);

		void *canpacket = payload + 10;
		while (canpacket < (void *)payload + m_len) {
			uint16_t canpacket_len = 0;
			memcpy_big_endian(&canpacket_len, canpacket, 2);
			canpacket += 2;
			void *canpacket_end = canpacket + canpacket_len;
			while (canpacket < canpacket_end) {
				uint64_t can_timestamp = 0;
				memcpy_big_endian(&can_timestamp, canpacket, 8);
				canpacket += 8;
				uint32_t canid = 0;
				memcpy_big_endian(&canid, canpacket, 4);
				canpacket += 4;
				uint8_t busid = 0;
				memcpy_big_endian(&busid, canpacket, 1);
				canpacket += 1;
				canpacket += 1; //pass direction
				uint16_t canPayloadLen = 0;
				memcpy_big_endian(&canPayloadLen, canpacket, 2);
				canpacket += 2;
				void *canPayload = NULL;
				canPayload = nng_alloc(canPayloadLen);
				memcpy_big_endian(canPayload, canpacket, canPayloadLen);
				canpacket += canPayloadLen;
				canStreamRowAddColumn(&stream->row[i], canid, busid, canPayloadLen, canPayload);
			}
		}
	}
	printCanStream(stream);

	return stream;
}
