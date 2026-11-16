// Copyright 2026 NanoMQ Team, Inc.
// SPDX-License-Identifier: MIT
#ifndef NNG_SOMEIP_STREAM_H
#define NNG_SOMEIP_STREAM_H

#include "nng/exchange/stream/stream.h"

/* exchange.streamType: 22 omits the digest; 23 verifies the full batch with
 * SHA-256. Both preserve complete PDUs, window keys and relative timestamps. */
#define SOMEIP_STREAM_ID 22
#define SOMEIP_CHECKED_STREAM_ID 23

#ifdef __cplusplus
extern "C" {
#endif

typedef struct someip_record {
	uint64_t ts;
	uint64_t batch_index;
	uint32_t frame_index;
	const uint8_t *frame;
	uint32_t size;
} someip_record;

/* Available with Parquet enabled. Each frame contains a complete SOME/IP PDU.
 * Records must have nondecreasing batch_index; equal keys are frames from one
 * window. Timestamps and keys must fit in INT64_MAX.
 * The returned batch owns a copy; release it with parquet_data_free(). */
NNG_DECL parquet_data *someip_encode_records(const someip_record *, uint32_t, bool checked);
/* MQTT payload: BE32 count, followed by count records of BE32 relative_us,
 * BE16 PDU size and the complete SOME/IP PDU (including its 16-byte header).
 * The PDU size is at most 65535 bytes, including the header; relative_us is
 * an unsigned 32-bit microsecond offset (0 through UINT32_MAX).
 * Empty/malformed windows reject the whole batch; input keys must increase.
 * Output: ts, batch_index, frame_index, session_id, message_template[_v2/_v3].
 * Session bytes are zeroed in the template and restored from session_id. */
NNG_DECL void *someip_encode(void *);
NNG_DECL void *someip_checked_encode(void *);
/* Decode a parquet_data_ret produced by the SOME/IP file reader. */
NNG_DECL void *someip_decode(void *);
/* Decode an encoded batch before writing; selects complete window keys. */
NNG_DECL void *someip_decode_range(parquet_data *, uint64_t start, uint64_t end);
NNG_DECL void *someip_cmd_parser(void *);
NNG_DECL int someip_stream_register(void);

#ifdef __cplusplus
}
#endif
#endif
