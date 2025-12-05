#include <arrow/io/file.h>
#include <parquet/stream_reader.h>
#include <parquet/stream_writer.h>

#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/md5.h"
#include "nng/supplemental/nanolib/parquet.h"
#include "nng/exchange/stream/stream.h"
#include "nng/exchange/stream/raw_stream.h"
#include "nng/supplemental/nanolib/queue.h"
#include "parquet_file_manager.h"
#include <assert.h>
#include <atomic>
#include <dirent.h>
#include <fstream>
#include <inttypes.h>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <vector>
#include <dirent.h>
#include <regex.h>
#include <unordered_map>
#include <queue>
using namespace std;
using parquet::ConvertedType;
using parquet::Encoding;
using parquet::Repetition;
using parquet::Type;
using parquet::schema::GroupNode;
using parquet::schema::PrimitiveNode;
#define PARQUET_END 1024

struct SchemaColumn {
	char                             *name;
	shared_ptr<parquet::ColumnReader> reader;
};

struct parquet_data {
	// Payload_arr should col first.
	// First column of schema should be
	// ts, can not be changed.
	// col_len is payload_arr col_len,
	// schema len = col_len + 1, 1 is ts col.
	uint32_t               col_len;
	uint32_t               row_len;
	uint64_t              *ts;
	char                 **schema;
	parquet_data_packet ***payload_arr;
};


#define DO_IT_IF_NOT_NULL(func, arg1, arg2) \
	if (arg1) {                         \
		func(arg1, arg2);           \
	}

#define FREE_IF_NOT_NULL(free, size) DO_IT_IF_NOT_NULL(nng_free, free, size)

#define _Atomic(X) std::atomic<X>
atomic_bool is_available = false;
#define WAIT_FOR_AVAILABLE    \
	while (!is_available) \
		nng_msleep(10);

#define UINT64_MAX_DIGITS 20

parquet_file_manager file_manager;
CircularQueue        parquet_queue;
pthread_mutex_t      parquet_queue_mutex     = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t       parquet_queue_not_empty = PTHREAD_COND_INITIALIZER;

// Streaming control: per-topic flags & worker context for streaming writes.
struct StreamingCtrl {
	nng_mtx *mtx;
	bool     running;
	bool     flush_now;
	// Dedicated per-topic worker thread & queue for streaming parquet writes.
	std::queue<parquet_object *> q;
	pthread_mutex_t              q_mtx;
	pthread_cond_t               q_cv;
	bool                         thread_started;
	bool                         stop;
	pthread_t                    thread;

	StreamingCtrl()
	    : mtx(nullptr)
	    , running(false)
	    , flush_now(false)
	    , thread_started(false)
	    , stop(false)
	{
		nng_mtx_alloc(&mtx);
		pthread_mutex_init(&q_mtx, NULL);
		pthread_cond_init(&q_cv, NULL);
	}
	~StreamingCtrl() {
		if (mtx) {
			nng_mtx_free(mtx);
		}
		pthread_mutex_destroy(&q_mtx);
		pthread_cond_destroy(&q_cv);
	}
};
static std::unordered_map<std::string, StreamingCtrl *> g_stream_ctrl;
static pthread_mutex_t g_stream_ctrl_mutex = PTHREAD_MUTEX_INITIALIZER;

static StreamingCtrl *get_stream_ctrl(const std::string &topic)
{
	pthread_mutex_lock(&g_stream_ctrl_mutex);
	auto it = g_stream_ctrl.find(topic);
	if (it == g_stream_ctrl.end()) {
		auto ctrl = new StreamingCtrl();
		g_stream_ctrl.emplace(topic, ctrl);
		pthread_mutex_unlock(&g_stream_ctrl_mutex);
		return ctrl;
	}
	pthread_mutex_unlock(&g_stream_ctrl_mutex);
	return it->second;
}

// Forward declarations for writer helpers used by per-topic streaming workers.
int  parquet_write_tmp(parquet_object *elem);
int  parquet_write(parquet_object *elem);

// Dedicated worker thread for a single streaming topic. It consumes
// parquet_object tasks from the per-topic queue and performs the actual
// write (streaming or non-streaming).
static void *
parquet_stream_worker(void *arg)
{
	StreamingCtrl *ctrl = (StreamingCtrl *) arg;

	for (;;) {
		pthread_mutex_lock(&ctrl->q_mtx);
		while (ctrl->q.empty() && !ctrl->stop) {
			pthread_cond_wait(&ctrl->q_cv, &ctrl->q_mtx);
		}
		if (ctrl->stop && ctrl->q.empty()) {
			pthread_mutex_unlock(&ctrl->q_mtx);
			break;
		}
		parquet_object *ele = ctrl->q.front();
		ctrl->q.pop();
		pthread_mutex_unlock(&ctrl->q_mtx);

		if (ele == nullptr) {
			continue;
		}

		// For streaming tmp writes WRITE_TEMP_RAW we reuse parquet_write_tmp(),
		// which internally dispatches to parquet_write_streaming().
		if (ele->type == WRITE_TEMP_RAW) {
			parquet_write_tmp(ele);
		} else {
			// Fallback for non-streaming tasks if ever enqueued here.
			switch (ele->type) {
			case WRITE_RAW:
			case WRITE_CAN:
				parquet_write(ele);
				break;
			default:
				parquet_object_free(ele);
				break;
			}
		}
	}

	return NULL;
}

// Enqueue a streaming parquet_object into the per-topic worker queue,
// starting the worker thread on first use. This gives each topic/stream
// its own dedicated thread processing its streaming writes sequentially.
static int
parquet_stream_enqueue(parquet_object *elem)
{
	if (elem == nullptr || elem->topic == nullptr) {
		return -1;
	}

	StreamingCtrl *ctrl = get_stream_ctrl(std::string(elem->topic));
	if (ctrl == nullptr) {
		return -1;
	}

	bool need_start = false;
	pthread_mutex_lock(&ctrl->q_mtx);
	if (!ctrl->thread_started) {
		ctrl->thread_started = true;
		need_start           = true;
	}
	// Push task into this topic's queue.
	ctrl->q.push(elem);
	pthread_cond_signal(&ctrl->q_cv);
	pthread_mutex_unlock(&ctrl->q_mtx);

	// Create the worker thread lazily on first enqueue.
	if (need_start) {
		int ret = pthread_create(
		    &ctrl->thread, NULL, parquet_stream_worker, ctrl);
		if (ret != 0) {
			log_error(
			    "Failed to create parquet stream thread for topic %s",
			    elem->topic);
			// Fallback: perform streaming write synchronously here.
			parquet_write_tmp(elem);
			return -1;
		}
		pthread_detach(ctrl->thread);
	}

	return 0;
}

static void parquet_stream_release_ctrl(StreamingCtrl *ctrl)
{
	if (ctrl == nullptr || ctrl->mtx == nullptr) {
		return;
	}
	nng_mtx_lock(ctrl->mtx);
	// End the current streaming write cycle, clear the accelerate flag.
	if (ctrl->running || ctrl->flush_now) {
		log_info("parquet stream: release ctrl, running=%d flush_now=%d",
		    (int) ctrl->running, (int) ctrl->flush_now);
	}
	ctrl->running   = false;
	ctrl->flush_now = false;
	nng_mtx_unlock(ctrl->mtx);
}

// Helper API for external modules: ask streaming writer of a topic to finish
// as soon as possible by setting flush_now flag.
void
parquet_stream_force_flush(const char *topic)
{
	if (topic == nullptr) {
		return;
	}
	StreamingCtrl *ctrl = get_stream_ctrl(std::string(topic));
	if (ctrl == nullptr || ctrl->mtx == nullptr) {
		return;
	}
	nng_mtx_lock(ctrl->mtx);
	// If there is already a force_flush request pending (flush_now still true),
	// don't set it again to avoid unnecessary repetition.
	if (!ctrl->flush_now) {
		log_info("parquet stream: force_flush requested for topic %s",
		    topic);
		ctrl->flush_now = true;
	} else {
		log_info("parquet stream: force_flush for topic %s ignored, "
		          "already pending",
		    topic);
	}
	nng_mtx_unlock(ctrl->mtx);
}

// Forward declarations for helpers defined elsewhere in this file
static shared_ptr<GroupNode> setup_schema(char **schema, uint32_t schema_col);
shared_ptr<parquet::FileEncryptionProperties> parquet_set_encryption(conf_parquet *conf);
static char *get_file_name(conf_parquet *conf, uint64_t key_start, uint64_t key_end);
char *compute_and_rename_file_withMD5(const char *filename, const conf_parquet *conf, const char *topic);
void update_parquet_file_ranges(conf_parquet *conf, parquet_object *elem, parquet_file_range *range);

// Streaming writer context (multi-column)
struct streaming_ctx {
	shared_ptr<parquet::ParquetFileWriter> file_writer;
	uint32_t                               col_num; // data columns count (exclude ts)
	bool                                   inited;
	uint64_t                               first_ts;
	uint64_t                               last_ts;
	uint32_t                               total_rows;
	size_t                                  total_bytes;
	size_t                                  written_bytes;
	uint64_t                                budget_ms;
	std::chrono::steady_clock::time_point   t0;
	conf_parquet                           *conf;
	char                                   *tmp_name;
	StreamingCtrl                          *ctrl;
	// sync
	nng_aio                                *aio;
	nng_mtx                                *mtx;
	nng_cv                                 *cv;
	bool                                    done;
	int                                     status;
};

static int parquet_stream_open(streaming_ctx *ctx, parquet_data *first_chunk)
{
	shared_ptr<GroupNode> schema = setup_schema(first_chunk->schema, first_chunk->col_len);
	parquet::WriterProperties::Builder builder;
	builder.compression(static_cast<arrow::Compression::type>(ctx->conf->comp_type));
	if (ctx->conf->encryption.enable) {
		shared_ptr<parquet::FileEncryptionProperties> enc;
		enc = parquet_set_encryption(ctx->conf);
		builder.encryption(enc);
	}
	shared_ptr<parquet::WriterProperties> props = builder.build();
	using FileClass = arrow::io::FileOutputStream;
	shared_ptr<FileClass> out_file;
	PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(ctx->tmp_name));
	ctx->file_writer = parquet::ParquetFileWriter::Open(out_file, schema, props);
	ctx->col_num     = (first_chunk->col_len > 0) ? (first_chunk->col_len - 1) : 0;
	ctx->inited = true;
	ctx->t0 = std::chrono::steady_clock::now();
	return 0;
}

static inline void parquet_stream_write_ts(streaming_ctx *ctx, parquet_data *chunk, parquet::Int64Writer *iw)
{
	for (uint32_t r = 0; r < chunk->row_len; r++) {
		int16_t def = 1;
		int64_t ts = (int64_t) chunk->ts[r];
		iw->WriteBatch(1, &def, nullptr, &ts);
	}
	if (ctx->total_rows == 0 && chunk->row_len > 0) ctx->first_ts = chunk->ts[0];
	if (chunk->row_len > 0) ctx->last_ts = chunk->ts[chunk->row_len - 1];
	ctx->total_rows += chunk->row_len;
}

// Write a single data column. Do NOT keep ColumnWriter pointers across columns:
// RowGroupSerializer::NextColumn may close/free the previous ColumnWriter, which
// would cause use-after-free if reused.
static inline size_t parquet_stream_write_one_column(
    parquet_data *chunk, parquet::ByteArrayWriter *writer, uint32_t col_idx)
{
	size_t batch_bytes = 0;
	for (uint32_t r = 0; r < chunk->row_len; r++) {
		if (chunk->payload_arr[col_idx][r] == NULL ||
		    chunk->payload_arr[col_idx][r]->size == 0) {
			int16_t def0 = 0;
			writer->WriteBatch(1, &def0, nullptr, nullptr);
		} else {
			int16_t          def = 1;
			parquet::ByteArray v;
			v.ptr = chunk->payload_arr[col_idx][r]->data;
			v.len = chunk->payload_arr[col_idx][r]->size;
			writer->WriteBatch(1, &def, nullptr, &v);
			batch_bytes += chunk->payload_arr[col_idx][r]->size;
		}
	}
	return batch_bytes;
}

static inline void parquet_stream_throttle(streaming_ctx *ctx)
{
	bool skip_throttle = false;
	if (ctx->ctrl) {
		nng_mtx_lock(ctx->ctrl->mtx);
		skip_throttle = ctx->ctrl->flush_now;
		nng_mtx_unlock(ctx->ctrl->mtx);
	}
	if (!skip_throttle) {
		nng_msleep(200);
	}
}

static void parquet_stream_aio_cb(void *arg)
{
	streaming_ctx *ctx = (streaming_ctx *) arg;
	nng_aio *aio = ctx->aio;
	parquet_data *chunk = (parquet_data *) nng_aio_get_prov_data(aio);
	bool is_last = (bool)(uintptr_t) nng_aio_get_output(aio, 0);
	int result = nng_aio_result(aio);
	if (result != 0) {
		if (chunk != NULL) {
			parquet_data_free(chunk);
		}
		nng_mtx_lock(ctx->mtx);
		ctx->status = result;
		ctx->done   = true;
		nng_cv_wake(ctx->cv);
		nng_mtx_unlock(ctx->mtx);
		return;
	}
	if (chunk != NULL) {
		if (!ctx->inited) {
			(void) parquet_stream_open(ctx, chunk);
		}
		parquet::RowGroupWriter *rg = ctx->file_writer->AppendRowGroup();
		// First write the ts column, then fetch and write each data column.
		// Note: RowGroupSerializer::NextColumn automatically closes/frees the
		// previous ColumnWriter when a new one is requested, so we MUST NOT
		// keep ColumnWriter pointers across columns, otherwise use-after-free
		// will occur.
		parquet::Int64Writer *iw =
		    static_cast<parquet::Int64Writer *>(rg->NextColumn());
		parquet_stream_write_ts(ctx, chunk, iw);
		if (iw) {
			iw->Close();
		}

		// For each data column, fetch a ByteArrayWriter, write this column and
		// close it immediately to avoid holding stale pointers across columns.
		for (uint32_t c = 0; c < ctx->col_num; c++) {
			parquet::ByteArrayWriter *writer =
			    static_cast<parquet::ByteArrayWriter *>(rg->NextColumn());
			if (writer == nullptr) {
				continue;
			}
			ctx->written_bytes += parquet_stream_write_one_column(
			    chunk, writer, c);
			writer->Close();
		}

		parquet_stream_throttle(ctx);
		rg->Close();

		parquet_stream_throttle(ctx);
		parquet_data_free(chunk);
	}
	if (is_last) {
		nng_mtx_lock(ctx->mtx);
		ctx->done = true;
		nng_cv_wake(ctx->cv);
		nng_mtx_unlock(ctx->mtx);
	}
}

static int parquet_stream_finalize(streaming_ctx *ctx, parquet_object *elem)
{
	if (ctx->file_writer) ctx->file_writer->Close();
	char *final_name = get_file_name(ctx->conf, ctx->first_ts, ctx->last_ts);
	if (final_name == NULL) {
		parquet_object_free(elem);
		free(ctx->tmp_name);
		return -1;
	}
	if (rename(ctx->tmp_name, final_name) != 0) {
		log_error("Failed to rename tmp %s to %s errno: %d", ctx->tmp_name, final_name, errno);
		free(final_name);
		final_name = ctx->tmp_name;
	} else {
		free(ctx->tmp_name);
	}
	char *md5_file_name = compute_and_rename_file_withMD5(final_name, ctx->conf, elem->topic);
	if (md5_file_name == nullptr) {
		parquet_object_free(elem);
		return -1;
	}
	parquet_file_range *range = parquet_file_range_alloc(
		0, ctx->total_rows > 0 ? (ctx->total_rows - 1) : 0, md5_file_name);
	update_parquet_file_ranges(ctx->conf, elem, range);
	pthread_mutex_lock(&parquet_queue_mutex);
	file_manager.update_queue(elem->topic, md5_file_name);
	pthread_mutex_unlock(&parquet_queue_mutex);
	return 0;
}

static char *
get_file_name(conf_parquet
 *conf, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;
	char *dir       = conf->dir;
	char *prefix    = conf->file_name_prefix;

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}


	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	return file_name;
}

string
gen_random(const int len)
{
	static const char alphanum[] = "0123456789"
	                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                               "abcdefghijklmnopqrstuvwxyz";
	string       tmp_s;
	tmp_s.reserve(len);

	for (int i = 0; i < len; ++i) {
		tmp_s += alphanum[rand() % (sizeof(alphanum) - 1)];
	}
	return tmp_s;
}

static char *
get_random_file_name(const char *dir, char *prefix, uint64_t key_start, uint64_t key_end)
{
	char *file_name = NULL;

	file_name = (char *) malloc(strlen(prefix) + strlen(dir) +
	    UINT64_MAX_DIGITS + UINT64_MAX_DIGITS + 16);
	if (file_name == NULL) {
		log_error("Failed to allocate memory for file name.");
		return NULL;
	}

	sprintf(file_name, "%s/%s-%" PRIu64 "~%" PRIu64 ".parquet", dir,
	    prefix, key_start, key_end);
	log_error("file_name: %s", file_name);
	return file_name;
}

static shared_ptr<GroupNode>
setup_schema(char **schema, uint32_t schema_col)
{
	parquet::schema::NodeVector fields;
	if (NULL == schema[0]) {
		log_error("Schema value is NULL!");
		return NULL;
	}
	// Set ts column
	fields.push_back(
	    PrimitiveNode::Make(schema[0], parquet::Repetition::REQUIRED,
	        parquet::Type::INT64, parquet::ConvertedType::UINT_64));

	// Set data column(like canid+busid or raw data...)
	for (uint32_t i = 1; i < schema_col; i++) {
		if (NULL == schema[i]) {
			log_error("Schema value is NULL!");
			return NULL;
		}
		fields.push_back(
		    PrimitiveNode::Make(schema[i], Repetition::OPTIONAL,
		        Type::BYTE_ARRAY, ConvertedType::NONE));
	}

	return static_pointer_cast<GroupNode>(
	    GroupNode::Make("schema", Repetition::REQUIRED, fields));
}

parquet_file_range *
parquet_file_range_alloc(uint32_t start_idx, uint32_t end_idx, char *filename)
{
	parquet_file_range *range = new parquet_file_range;
	range->start_idx          = start_idx;
	range->end_idx            = end_idx;
	range->filename           = nng_strdup(filename);
	return range;
}

void
parquet_file_range_free(parquet_file_range *range)
{
	if (range) {
		FREE_IF_NOT_NULL(range->filename, strlen(range->filename));
		delete range;
	}
}

parquet_data *
parquet_data_alloc(char **schema, parquet_data_packet ***payload_arr,
    uint64_t *ts, uint32_t col_len, uint32_t row_len)
{
	if (payload_arr == NULL || schema == NULL || col_len == 0 ||
	    row_len == 0) {
		log_error("payload || schema should not be NULL, col || row "
		          "len should't == 0");
	}
	parquet_data *data = new parquet_data;
	if (data == NULL) {
		return NULL; // Memory allocation failed
	}
	data->ts          = ts;
	data->col_len     = col_len + 1;
	data->row_len     = row_len;
	data->schema      = schema;
	data->payload_arr = payload_arr;
	return data;
}

void
parquet_data_free(parquet_data *data)
{
	if (data) {
		for (uint32_t c = 0; c < data->col_len - 1; c++) {
			FREE_IF_NOT_NULL(
			    data->schema[c], strlen(data->schema[c]));
			for (uint32_t r = 0; r < data->row_len; r++) {
				parquet_data_packet *payload =
				    data->payload_arr[c][r];
				if (payload && payload->data && payload->size > 0) {
					nng_free(payload->data, payload->size);
				}
				FREE_IF_NOT_NULL(payload, sizeof(*payload));
			}
			FREE_IF_NOT_NULL(data->payload_arr[c], data->row_len);
		}

		FREE_IF_NOT_NULL(data->schema[data->col_len - 1],
		    strlen(data->schema[data->col_len - 1]));
		FREE_IF_NOT_NULL(data->schema, data->col_len);
		FREE_IF_NOT_NULL(data->ts, data->row_len);
		FREE_IF_NOT_NULL(data->payload_arr, data->col_len);
		delete data;
	}
}

parquet_object *
parquet_object_alloc(parquet_data *data, parquet_type type, nng_aio *aio,
    void *aio_arg, char *topic)
{
	parquet_object *elem = new parquet_object;
	elem->data           = data;
	elem->type           = type;
	elem->aio            = aio;
	elem->aio_arg        = aio_arg;
	elem->topic          = topic;
	elem->ranges        = new parquet_file_ranges;
	elem->ranges->range = NULL;
	elem->ranges->start = 0;
	elem->ranges->size  = 0;
	return elem;
}

void
parquet_object_free(parquet_object *elem)
{
	if (elem) {
		if (elem->data) {
			parquet_data_free(elem->data);
		}
		nng_aio_set_prov_data(elem->aio, elem->aio_arg);
		nng_aio_set_output(elem->aio, 1, elem->ranges);
		log_debug("finish write aio");
		DO_IT_IF_NOT_NULL(nng_aio_finish_sync, elem->aio, 0);

		for (int i = 0; i < elem->ranges->size; i++) {
			parquet_file_range_free(elem->ranges->range[i]);
		}
		free(elem->ranges->range);
		delete elem->ranges;
		delete elem;
	}
}

int
parquet_write_batch_async(parquet_object *elem)
{
	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	// Streaming path: hand off to per-topic worker thread so that each
	// topic/stream has its own dedicated writer thread.
	if (elem->type == WRITE_TEMP_RAW) {
		if (elem->topic) {
			// If a new streaming task arrives while previous is still
			// running, set flush_now so the current streaming writer
			// will try to finish as soon as possible.
			StreamingCtrl *ctrl = get_stream_ctrl(elem->topic);
			nng_mtx_lock(ctrl->mtx);
			if (ctrl->running) {
				ctrl->flush_now = true;
			}
			nng_mtx_unlock(ctrl->mtx);
		}
		return parquet_stream_enqueue(elem);
	}

	// Non-streaming path: use global parquet_queue and single worker
	// thread (see parquet_write_launcher/parquet_write_loop_v2).
	log_debug("WAIT_FOR_AVAILABLE");
	WAIT_FOR_AVAILABLE
	log_debug("WAIT_FOR parquet_queue_mutex");
	pthread_mutex_lock(&parquet_queue_mutex);
	if (IS_EMPTY(parquet_queue)) {
		pthread_cond_broadcast(&parquet_queue_not_empty);
		log_debug("broadcast signal!");
	}
	ENQUEUE(parquet_queue, elem);
	log_debug("enqueue element.");

	pthread_mutex_unlock(&parquet_queue_mutex);

	return 0;
}

int
parquet_write_batch_tmp_async(parquet_object *elem)
{
	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	elem->type = WRITE_TEMP_RAW;
	// Reuse the same streaming async path as parquet_write_batch_async.
	return parquet_write_batch_async(elem);
}

shared_ptr<parquet::FileEncryptionProperties>
parquet_set_encryption(conf_parquet *conf)
{

	shared_ptr<parquet::FileEncryptionProperties>
	    encryption_configurations;

	// Encrypt all columns and the footer with
	// the same key. (uniform encryption)
	parquet::FileEncryptionProperties::Builder file_encryption_builder(
	    conf->encryption.key);
	encryption_configurations =
	    file_encryption_builder
	        .footer_key_metadata(conf->encryption.key_id)
	        ->algorithm(static_cast<parquet::ParquetCipher::type>(
	            conf->encryption.type))
	        ->build();

	return encryption_configurations;
}

void
update_parquet_file_ranges(
    conf_parquet *conf, parquet_object *elem, parquet_file_range *range)
{
	if (elem->ranges->size != (int) conf->file_count) {
		elem->ranges->range =
		    (parquet_file_range **) realloc(elem->ranges->range,
		        sizeof(parquet_file_range *) * (++elem->ranges->size));
		elem->ranges->range[elem->ranges->size - 1] = range;
	} else {
		// Free old ranges and insert new ranges
		// update start index
		parquet_file_range_free(
		    elem->ranges->range[elem->ranges->start]);
		elem->ranges->range[elem->ranges->start] = range;
		elem->ranges->start++;
		elem->ranges->start %= elem->ranges->size;
	}
}

std::string
compute_and_rename_file_withMD5_CXX(const std::string &filename,
    const conf_parquet *conf, const std::string &topic)
{
	char md5_buffer[MD5_LEN + 1] = { 0 };
	log_debug("Computing MD5...");

	// Step 1: Compute MD5 checksum of the file
	if (ComputeFileMD5(filename.c_str(), md5_buffer) != 0) {
		log_error("Failed to calculate md5sum");
		if (remove(filename.c_str()) != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename.c_str(), errno);
		}
		return {};
	}

	// Step 2: Extract timestamp substring from the original filename
	std::string prefix =
	    std::string(conf->dir) + "/" + conf->file_name_prefix;
	size_t ts_start_pos =
	    prefix.size() + 1; // assumes an extra separator ("/" or "_")
	size_t ts_end_pos = filename.rfind('.');
	if (ts_end_pos == std::string::npos || ts_end_pos <= ts_start_pos) {
		log_error("Invalid filename format: %s", filename.c_str());
		if (remove(filename.c_str()) != 0) {
			log_error("Failed to remove file %s errno: %d",
			    filename.c_str(), errno);
		}

		return {};
	}
	std::string timestamp =
	    filename.substr(ts_start_pos, ts_end_pos - ts_start_pos);

	// Step 3: Get queue index for the topic
	uint32_t    index  = file_manager.get_queue_index(topic);
	std::string sindex = std::to_string(index);

	// Step 4: Build new filename:
	// <dir+prefix>_<topic>-<timestamp>_<md5>.parquet
	std::string new_name = prefix + "_" + topic + "-" + timestamp + "_" +
	    sindex + "_" + md5_buffer + ".parquet";

	// Step 5: Rename the file to the new name (fallback to copy if cross-device)
	log_info(
	    "Trying to rename %s to %s", filename.c_str(), new_name.c_str());
	if (rename(filename.c_str(), new_name.c_str()) != 0) {
		int err = errno;
		log_error("Failed to rename file %s to %s errno: %d",
		    filename.c_str(), new_name.c_str(), err);
		if (err == EXDEV || err == EEXIST) {
			std::ifstream src(filename, std::ios::binary);
			std::ofstream dst(new_name, std::ios::binary | std::ios::trunc);
			if (src && dst) {
				dst << src.rdbuf();
				dst.flush();
				src.close();
				dst.close();
				if (remove(filename.c_str()) != 0) {
					log_error("Failed to remove temp file %s errno: %d",
					    filename.c_str(), errno);
				}
			} else {
				log_error("Copy file failed from %s to %s", filename.c_str(), new_name.c_str());
				if (remove(filename.c_str()) != 0) {
					log_error("Failed to remove file %s errno: %d",
					    filename.c_str(), errno);
				}
				return {};
			}
		} else {
			if (remove(filename.c_str()) != 0) {
				log_error("Failed to remove file %s errno: %d",
				    filename.c_str(), errno);
			}
			return {};
		}
	}

	return new_name;
}

char *
compute_and_rename_file_withMD5(
    const char *filename, const conf_parquet *conf, const char *topic)
{
	std::string result =
	    compute_and_rename_file_withMD5_CXX(filename, conf, topic);
	if (filename) {
		free((void *) filename); // Free the original filename as it's no longer needed
	}
	if (result.empty()) {
		return NULL;
	}

	char *out = (char *) malloc(result.size() + 1);
	if (out)
		strcpy(out, result.c_str());
	return out;
}

int
parquet_write_core(conf_parquet *conf, char *filename,
    shared_ptr<GroupNode> schema, parquet_data *data)
{

	char                 **schema_arr  = data->schema;
	uint32_t               col_len     = data->col_len;
	uint32_t               row_len     = data->row_len;
	uint64_t              *ts_arr      = data->ts;
	parquet_data_packet ***payload_arr = data->payload_arr;

	string exception_msg = "";
	try {

		parquet::WriterProperties::Builder builder;
		log_debug("init builder");
		builder.created_by("NanoMQ")
		    ->version(parquet::ParquetVersion::PARQUET_2_6)
		    ->data_page_version(parquet::ParquetDataPageVersion::V2)
		    ->disable_dictionary()
		    ->encoding(parquet::Encoding::PLAIN)
		    ->encoding(schema_arr[0], Encoding::DELTA_BINARY_PACKED)
		    ->compression(static_cast<arrow::Compression::type>(
		        conf->comp_type));
		log_debug("check encry");
		if (conf->encryption.enable) {
			shared_ptr<parquet::FileEncryptionProperties>
			    encryption_configurations;
			encryption_configurations =
			    parquet_set_encryption(conf);
			builder.encryption(encryption_configurations);
		}

		shared_ptr<parquet::WriterProperties> props = builder.build();
		using FileClass = arrow::io::FileOutputStream;
		shared_ptr<FileClass> out_file;
		PARQUET_ASSIGN_OR_THROW(out_file, FileClass::Open(filename));
		shared_ptr<parquet::ParquetFileWriter> file_writer =
		    parquet::ParquetFileWriter::Open(out_file, schema, props);

		// Append a RowGroup with a specific number of rows.
		parquet::RowGroupWriter *rg_writer =
		    file_writer->AppendRowGroup();

		// Write the Int64 column
		log_debug("start doing int64 write");
		parquet::Int64Writer *int64_writer =
		    static_cast<parquet::Int64Writer *>(
		        rg_writer->NextColumn());
		for (uint32_t r = 0; r < row_len; r++) {
			int64_t value            = ts_arr[r];
			int16_t definition_level = 1;
			int64_writer->WriteBatch(
			    1, &definition_level, nullptr, &value);
		}
		log_debug("stop doing int64 write");

		// Write the ByteArray column. Make every alternate values NULL
		for (uint32_t c = 0; c < col_len - 1; c++) {
			parquet::ByteArrayWriter *ba_writer =
			    static_cast<parquet::ByteArrayWriter *>(
			        rg_writer->NextColumn());
			for (uint32_t r = 0; r < row_len; r++) {

				if (payload_arr[c][r] != NULL) {
					int16_t definition_level = 1;
					parquet::ByteArray value;
					value.ptr = payload_arr[c][r]->data;
					value.len = payload_arr[c][r]->size;
					ba_writer->WriteBatch(1,
					    &definition_level, nullptr,
					    &value);
				} else {
					int16_t definition_level = 0;
					ba_writer->WriteBatch(1,
					    &definition_level, nullptr,
					    nullptr);
				}
			}
		}
		// Close the RowGroupWriter
		rg_writer->Close();
		// Close the ParquetFileWriter
		file_writer->Close();
		log_debug("stop doing ByteArray write");

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return 0;
}

static int
parquet_write_streaming(parquet_object *elem)
{
	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	parquet_stream_in *sin = (parquet_stream_in *) elem->aio_arg;
	if (sin == nullptr || sin->magic != PARQUET_STREAM_IN_MAGIC || sin->sdata == nullptr) {
		log_error("streaming cbdata invalid");
		parquet_object_free(elem);
		return -1;
	}

	// Mark this topic streaming as running and observe flush-now flag if set later
	StreamingCtrl *ctrl = get_stream_ctrl(elem->topic);
	nng_mtx_lock(ctrl->mtx);
	ctrl->running = true;
	nng_mtx_unlock(ctrl->mtx);

	// Create a temporary filename under /tmp (avoid cross-device rename issues).
	string prefix  = gen_random(6);
	prefix         = "nanomq_stream_" + prefix;
	char *tmp_name = get_random_file_name(conf->dir, (char *) prefix.c_str(), 0, 0);
	if (tmp_name == NULL) {
		log_error("Failed to get temp file name");
		parquet_stream_release_ctrl(ctrl);
		parquet_object_free(elem);
		return -1;
	}

	try {
		streaming_ctx ctx;
		ctx.file_writer = nullptr;
		ctx.col_num = 0;
		ctx.inited = false;
		ctx.first_ts = 0;
		ctx.last_ts = 0;
		ctx.total_rows = 0;
		ctx.total_bytes = 0;
		ctx.written_bytes = 0;
		ctx.budget_ms = 0;
		ctx.t0 = std::chrono::steady_clock::now();
		ctx.conf = conf;
		ctx.tmp_name = tmp_name;
		ctx.ctrl = ctrl;
		ctx.aio = NULL;
		ctx.mtx = NULL;
		ctx.cv = NULL;
		ctx.done = false;
		ctx.status = 0;

		nng_mtx_alloc(&ctx.mtx);
		nng_cv_alloc(&ctx.cv, ctx.mtx);

		// callback moved to parquet_stream_aio_cb
		// Pre-calculate total bytes across all columns for throttling.
		if (sin && sin->sdata && sin->sdata->lens) {
			for (uint32_t i = 0; i < sin->sdata->len; ++i) {
				ctx.total_bytes += sin->sdata->lens[i];
			}
		}
		// run streaming
		nng_aio *laio = NULL;
		nng_aio_alloc(&laio, parquet_stream_aio_cb, &ctx);
		ctx.aio = laio;
		int encode_ret = stream_encode_stream(sin->stream_id, sin->sdata, laio, 500 * 1024);
		if (encode_ret != 0) {
			nng_aio_free(laio);
			nng_cv_free(ctx.cv);
			nng_mtx_free(ctx.mtx);
			if (ctx.tmp_name) {
				free(ctx.tmp_name);
				ctx.tmp_name = NULL;
				tmp_name = NULL;
			}
			parquet_stream_release_ctrl(ctrl);
			parquet_object_free(elem);
			return encode_ret;
		}
		nng_mtx_lock(ctx.mtx);
		while (!ctx.done) {
			nng_cv_wait(ctx.cv);
		}
		nng_mtx_unlock(ctx.mtx);
		nng_aio_free(laio);
		nng_cv_free(ctx.cv);
		nng_mtx_free(ctx.mtx);

		if (ctx.status != 0) {
			if (ctx.file_writer) ctx.file_writer->Close();
			if (ctx.tmp_name) {
				remove(ctx.tmp_name);
				free(ctx.tmp_name);
				ctx.tmp_name = NULL;
			}
			parquet_stream_release_ctrl(ctrl);
			parquet_object_free(elem);
			return ctx.status;
		}

		if (parquet_stream_finalize(&ctx, elem) != 0) {
			parquet_stream_release_ctrl(ctrl);
			return -1;
		}

		parquet_stream_release_ctrl(ctrl);

		parquet_object_free(elem);
		return 0;
	} catch (const exception &e) {
		log_error("streaming tmp write exception: %s", e.what());
		if (tmp_name) {
			remove(tmp_name);
			free(tmp_name);
			tmp_name = NULL;
		}
		parquet_stream_release_ctrl(ctrl);
		parquet_object_free(elem);
		return -1;
	}
}

int
parquet_write_tmp(parquet_object *elem)
{

	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	// Streaming write: when data is NULL and aio_arg is provided.
	if (elem->data == nullptr && elem->aio != nullptr) {
		return parquet_write_streaming(elem);
	}

	// Non-streaming (legacy) write path.
	char    **schema_arr = elem->data->schema;
	uint32_t  col_len    = elem->data->col_len;
	uint32_t  row_len    = elem->data->row_len;
	uint64_t *ts_arr     = elem->data->ts;

	shared_ptr<GroupNode> schema = setup_schema(schema_arr, col_len);

	if (NULL == schema) {
		log_error("Schema set error.");
		return -1;
	}

	log_debug("parquet_write");

	string prefix  = gen_random(6);
	prefix         = "nanomq" + prefix;
	char *filename = get_random_file_name(
	    conf->dir, prefix.data(), ts_arr[0], ts_arr[row_len - 1]);
	if (filename == NULL) {
		log_error("Failed to get file name");
		parquet_object_free(elem);
		return -1;
	}

	parquet_write_core(conf, filename, schema, elem->data);
	parquet_file_range *range =
	    parquet_file_range_alloc(0, row_len - 1, filename);
	free(filename);
	update_parquet_file_ranges(conf, elem, range);

	// Create a ParquetFileWriter instance
	parquet::WriterProperties::Builder builder;

	parquet_object_free(elem);
	return 0;
}

int
parquet_write(parquet_object *elem)
{

	conf_parquet *conf = file_manager.fetch_conf(elem->topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", elem->topic);
		return -1;
	}

	char    **schema_arr = elem->data->schema;
	uint32_t  col_len    = elem->data->col_len;
	uint32_t  row_len    = elem->data->row_len;
	uint64_t *ts_arr     = elem->data->ts;

	shared_ptr<GroupNode> schema = setup_schema(schema_arr, col_len);

	if (NULL == schema) {
		log_error("Schema set error.");
		return -1;
	}

	log_debug("parquet_write");
	char *filename = get_file_name(conf, ts_arr[0], ts_arr[row_len - 1]);
	if (filename == NULL) {
		parquet_object_free(elem);
		log_error("Failed to get file name");
		return -1;
	}

	parquet_write_core(conf, filename, schema, elem->data);
	char *md5_file_name =
	    compute_and_rename_file_withMD5(filename, conf, elem->topic);
	if (md5_file_name == nullptr) {
		parquet_object_free(elem);
		log_error("fail to get md5 from parquet file");
		return -1;
	}

	char md5_buffer[MD5_LEN + 1];
	log_debug("compute md5 after rename");
	int ret = ComputeFileMD5(md5_file_name, md5_buffer);
	if (ret != 0) {
		log_error("Failed to calculate md5sum");
	}

	parquet_file_range *range = parquet_file_range_alloc(
	    0, elem->data->row_len - 1, md5_file_name);
	update_parquet_file_ranges(conf, elem, range);

	log_debug("wait for parquet_queue_mutex");
	pthread_mutex_lock(&parquet_queue_mutex);
	file_manager.update_queue(elem->topic, md5_file_name);
	pthread_mutex_unlock(&parquet_queue_mutex);

	log_info("flush finished!");
	parquet_object_free(elem);
	return 0;
}

void *
parquet_write_loop_v2(void *arg)
{
	(void(arg));

	while (true) {
		// wait for mqtt messages to send method request
		pthread_mutex_lock(&parquet_queue_mutex);

		while (IS_EMPTY(parquet_queue)) {
			pthread_cond_wait(
			    &parquet_queue_not_empty, &parquet_queue_mutex);
		}

		log_debug("fetch element from parquet queue");
		parquet_object *ele =
		    (parquet_object *) DEQUEUE(parquet_queue);

		pthread_mutex_unlock(&parquet_queue_mutex);

		switch (ele->type) {
		case WRITE_RAW:
		case WRITE_CAN:
			parquet_write(ele);
			break;
		case WRITE_TEMP_RAW:
			parquet_write_tmp(ele);
			break;
		default:
			break;
		}
	}
	return NULL;
}


int
parquet_write_launcher(conf_exchange *conf)
{

	INIT_QUEUE(parquet_queue);

	for (size_t i = 0; i < conf->count; i++) {
		file_manager.add_queue(conf->nodes[i]);
	}

	is_available = true;
	pthread_t write_thread;
	int       result = 0;
	result =
	    pthread_create(&write_thread, NULL, parquet_write_loop_v2, conf);
	if (result != 0) {
		log_error("Failed to create parquet write thread.");
		return -1;
	}

	return 0;
}

static void
get_range(const char *name, uint64_t range[2])
{
	// {prefix}_{topic}-{start_ts}~{end_ts}_{md5}.parquet
	const char *ts_start = strrchr(name, '-');
	if (!ts_start) {
		range[0] = range[1] = 0;
		return;
	}
	ts_start++;

	char md5[33] = { 0 };

	if (sscanf(ts_start, "%lu~%lu_%32[^.]", &range[0], &range[1], md5) !=
	    3) {
		range[0] = range[1] = 0;
	}
}

static bool
compare_callback(void *name, uint64_t key)
{
	uint64_t range[2] = { 0 };
	get_range((const char *) name, range);
	return (key >= range[0] && key <= range[1]);
}

static bool
compare_callback_span(void *name, uint64_t low, uint64_t high)
{
	uint64_t range[2] = { 0 };
	get_range((const char *) name, range);
	return !(low > range[1] || high < range[0]);
}

const char *
parquet_find(const char *topic, uint64_t key)
{
	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	WAIT_FOR_AVAILABLE
	const char *value = NULL;
	void       *elem  = NULL;
	pthread_mutex_lock(&parquet_queue_mutex);
	auto queue = file_manager.fetch_queue(topic);
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem) {
			if (compare_callback(elem, key)) {
				value = nng_strdup((char *) elem);
				break;
			}
		}
	}
	pthread_mutex_unlock(&parquet_queue_mutex);
	return value;
}

const char **
parquet_find_span(
    const char *topic, uint64_t start_key, uint64_t end_key, uint32_t *size)
{

	if (start_key > end_key) {
		log_error("Start key can't be greater than end_key.");
		*size = 0;
		return NULL;
	}
	WAIT_FOR_AVAILABLE
	uint64_t     low        = start_key;
	uint64_t     high       = end_key;
	uint32_t     local_size = 0;
	const char  *value      = NULL;
	const char **array      = NULL;
	const char **ret        = NULL;
	void        *elem       = NULL;

	pthread_mutex_lock(&parquet_queue_mutex);

	auto queue = file_manager.fetch_queue(topic);
	if (queue->size != 0) {
		array = (const char **) nng_alloc(sizeof(char *) * queue->size);

		ret = array;
		FOREACH_QUEUE(*queue, elem)
		{
			if (elem) {
				if (compare_callback_span(elem, low, high)) {
					++local_size;
					value    = nng_strdup((char *) elem);
					*array++ = value;
				}
			}
		}
	}

	pthread_mutex_unlock(&parquet_queue_mutex);
	(*size) = local_size;
	return ret;
}

void
parquet_read_set_property(
    parquet::ReaderProperties &reader_properties, conf_parquet *conf)
{
	if (conf->encryption.enable) {
		map<string,
		    shared_ptr<parquet::ColumnDecryptionProperties>>
		    decryption_cols;
		parquet::FileDecryptionProperties::Builder
		    file_decryption_builder_3;
		shared_ptr<parquet::FileDecryptionProperties>
		    decryption_configuration =
		        file_decryption_builder_3
		            .footer_key(conf->encryption.key)
		            ->column_keys(decryption_cols)
		            ->build();

		// Add the current decryption configuration to
		// ReaderProperties.
		reader_properties.file_decryption_properties(
		    decryption_configuration->DeepClone());
	}

	return;
}

static uint8_t *
parquet_read(conf_parquet *conf, char *filename, uint64_t key, uint32_t *len)
{
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);

	// Create a ParquetReader instance
	string exception_msg = "";
	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups
		for (int r = 0; r < num_row_groups; ++r) {

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			int16_t repetition_level;
			shared_ptr<parquet::ColumnReader> column_reader;

			// Get the Column Reader for the Int64 column
			column_reader = row_group_reader->Column(0);
			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			int i = 0;
			while (int64_reader->HasNext()) {
				int64_t value;
				rows_read = int64_reader->ReadBatch(1,
				    &definition_level, &repetition_level,
				    &value, &values_read);
				if (1 == rows_read && 1 == values_read) {
					if (((uint64_t) value) == key)
						break;
				}
				i++;
			}

			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			parquet::ByteArrayReader *ba_reader =
			    static_cast<parquet::ByteArrayReader *>(
			        column_reader.get());

			if (ba_reader->HasNext()) {
				ba_reader->Skip(i);
			}

			if (ba_reader->HasNext()) {
				parquet::ByteArray value;
				rows_read =
				    ba_reader->ReadBatch(1, &definition_level,
				        nullptr, &value, &values_read);
				if (1 == rows_read && 1 == values_read) {
					uint8_t *ret = (uint8_t *) malloc(
					    value.len * sizeof(uint8_t));
					memcpy(ret, value.ptr, value.len);
					*len = value.len;
					return ret;
				}
			}
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return NULL;
}

static vector<int>
get_keys_indexes(
    parquet::Int64Reader *int64_reader, const vector<uint64_t> &keys)
{
	vector<int> index_vector;
	int64_t     values_read = 0;
	int64_t     rows_read   = 0;
	int16_t     definition_level;
	int16_t     repetition_level;

	int index = 0;
	for (const auto &key : keys) {
		bool found = false;
		while (int64_reader->HasNext()) {
			int64_t value;
			rows_read =
			    int64_reader->ReadBatch(1, &definition_level,
			        &repetition_level, &value, &values_read);
			if (1 == rows_read && 1 == values_read) {
				if (((uint64_t) value) == key) {
					index_vector.push_back(index++);
					found = true;
					break;
				}
			}
			index++;
		}
		if (!found) {
			index_vector.push_back(-1);
		}
	}

	return index_vector;
}

static vector<int>
get_keys_indexes_fuzing(
    parquet::Int64Reader *int64_reader, vector<uint64_t> &ts, uint64_t start_key, uint64_t end_key)
{
	vector<int> index_vector;
	int64_t     values_read = 0;
	int64_t     rows_read   = 0;
	int16_t     definition_level;
	int16_t     repetition_level;

	int  index = 0;
	bool found = false;

	log_debug("start_key: %lu, end_key: %lu", start_key, end_key);

	while (int64_reader->HasNext()) {
		int64_t value;
		rows_read = int64_reader->ReadBatch(1, &definition_level,
		    &repetition_level, &value, &values_read);
		if (1 == rows_read && 1 == values_read) {
			log_trace("read value: %lu", value);
			if (((uint64_t) value) >= start_key) {
				index_vector.push_back(index++);
				ts.push_back(value);
				found = true;
				break;
			}
			index++;
		}
	}
	if (!found) {
		index_vector.push_back(-1);
		index_vector.push_back(-1);
	} else {
		found = false;
		while (int64_reader->HasNext()) {
			int64_t value;
			rows_read =
			    int64_reader->ReadBatch(1, &definition_level,
			        &repetition_level, &value, &values_read);
			log_trace("read value: %lu", value);
			if (1 == rows_read && 1 == values_read) {
				if (((uint64_t) value) > end_key) {
					index_vector.push_back(index - 1);
					found = true;
					break;
				} else if (((uint64_t) value) == end_key) {
					index_vector.push_back(index);
					ts.push_back(value);
					found = true;
					break;
				} else {
					ts.push_back(value);
				}
				index++;
			}
		}

		if (!found) {
			index_vector.push_back(index - 1);
		}
	}

	return index_vector;
}

static vector<parquet_data_packet *>
parquet_read(conf_parquet *conf, char *filename, vector<uint64_t> keys)
{
	vector<parquet_data_packet *> ret_vec;
	string                   path_int64 = "key";
	string                   path_str   = "data";
	parquet::ReaderProperties     reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);
	vector<int> index_vector(keys.size());

	// Create a ParquetReader instance
	string exception_msg = "";
	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups

		for (int r = 0; r < num_row_groups; ++r) {

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			int64_t values_read = 0;
			int64_t rows_read   = 0;
			int16_t definition_level;
			shared_ptr<parquet::ColumnReader> column_reader;

			column_reader = row_group_reader->Column(0);
			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			index_vector = get_keys_indexes(int64_reader, keys);
			// Get the Column Reader for the ByteArray column
			column_reader = row_group_reader->Column(1);
			parquet::ByteArrayReader *ba_reader =
			    static_cast<parquet::ByteArrayReader *>(
			        column_reader.get());

			for (const auto &index : index_vector) {
				if (-1 == index) {
					ret_vec.push_back(NULL);
					continue;
				}

				if (ba_reader->HasNext()) {
					ba_reader->Skip(index - 1);
				}

				if (ba_reader->HasNext()) {
					parquet::ByteArray value;
					rows_read = ba_reader->ReadBatch(1,
					    &definition_level, nullptr, &value,
					    &values_read);
					if (1 == rows_read &&
					    1 == values_read) {
						parquet_data_packet *pack =
						    (parquet_data_packet *)
						        malloc(sizeof(
						            parquet_data_packet));
						pack->data =
						    (uint8_t *) malloc(
						        value.len *
						        sizeof(uint8_t));
						memcpy(pack->data, value.ptr,
						    value.len);
						pack->size = value.len;
						ret_vec.push_back(pack);
					}
				}
			}
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret_vec;
}

string extract_topic(const string &file_path) {
    // Define the regex pattern to match the file format
    std::regex pattern(R"(.*?/[^/]*_(.*?)_[a-fA-F0-9]{32}-\d+~\d+\.parquet)");
    std::smatch matches;

    // Perform regex matching
    if (std::regex_match(file_path, matches, pattern) && matches.size() > 1) {
        return matches[1]; // Return the captured 'topic' group
    }

    // If no match, return an empty string
    return "";
}

vector<parquet_data_packet *>
parquet_find_data_packet(
    conf_parquet *conf, char *filename, vector<uint64_t> keys)
{
	vector<parquet_data_packet *> ret_vec;
	string topic = extract_topic(filename);
	conf         = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic.c_str());
		return ret_vec;
	}


	WAIT_FOR_AVAILABLE
	void *elem = NULL;
	auto queue = file_manager.fetch_queue(topic);
	pthread_mutex_lock(&parquet_queue_mutex);
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem && nng_strcasecmp((char *) elem, filename) == 0) {
			goto find;
		}
	}

find:
	pthread_mutex_unlock(&parquet_queue_mutex);

	if (elem) {
		ret_vec = parquet_read(conf, (char *) elem, keys);
	} else {

		ret_vec.resize(keys.size(), nullptr);
		log_debug("Not find file %s in file queue", (char *) elem);
	}
	return ret_vec;
}

parquet_data_packet *
parquet_find_data_packet(conf_parquet *conf, char *filename, uint64_t key)
{
	string topic = extract_topic(filename);
	conf         = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic.c_str());
		return NULL;
	}
	WAIT_FOR_AVAILABLE
	void *elem  = NULL;
	auto  queue = file_manager.fetch_queue(topic);

	pthread_mutex_lock(&parquet_queue_mutex);
	FOREACH_QUEUE(*queue, elem)
	{
		if (elem && nng_strcasecmp((char *) elem, filename) == 0) {
			goto find;
		}
	}

find:
	pthread_mutex_unlock(&parquet_queue_mutex);

	if (elem) {
		uint32_t size = 0;
		uint8_t *data = parquet_read(conf, (char *) elem, key, &size);
		if (size) {
			parquet_data_packet *pack =
			    (parquet_data_packet *) malloc(
			        sizeof(parquet_data_packet));
			pack->data = data;
			pack->size = size;
			return pack;
		} else {
			log_debug(
			    "No key %ld in file: %s", key, (char *) elem);
		}
	}
	log_debug("Not find file %s in file queue", (char *) elem);
	return NULL;
}

parquet_data_packet **
parquet_find_data_packets(
    conf_parquet *conf, char **filenames, uint64_t *keys, uint32_t len)
{
	unordered_map<char *, vector<uint64_t>> file_name_map;
	vector<parquet_data_packet *>           ret_vec;
	parquet_data_packet                   **packets = NULL;
	// Get the file map
	for (uint32_t i = 0; i < len; i++) {
		vector<uint64_t> key_vec;
		if (filenames[i] == NULL) {
			log_error("filenames[%d] is NULL, len is %d", i, len);
			return NULL;
		}
		if (auto s = file_name_map.find(filenames[i]);
		    s != file_name_map.end()) {
			s->second.push_back(keys[i]);
		} else {
			key_vec.push_back(keys[i]);
			file_name_map.insert(pair(filenames[i], key_vec));
		}
	}

	// Traverse the map and get the vector of parquet_data_packet
	for (const auto &entry : file_name_map) {
		char                        *filename = entry.first;
		const vector<uint64_t> &sizes    = entry.second;

		auto tmp = parquet_find_data_packet(conf, filename, sizes);
		ret_vec.insert(ret_vec.end(), tmp.begin(), tmp.end());
	}

	if (!ret_vec.empty()) {
		packets = (parquet_data_packet **) malloc(
		    sizeof(parquet_data_packet *) * len);
		copy(ret_vec.begin(), ret_vec.end(), packets);
	}

	// return packets;
	return nullptr;
}

static vector<SchemaColumn>
get_filtered_schema(shared_ptr<parquet::RowGroupReader> row_group_reader,
    shared_ptr<parquet::FileMetaData> file_metadata, const char **schema,
    uint16_t schema_len)
{
	vector<SchemaColumn> schema_vec;
	int                  num_columns = file_metadata->num_columns();

	for (int i = 1; i < num_columns; i++) {
		const char *column_name =
		    file_metadata->schema()->Column(i)->name().c_str();
		if (schema_len > 0) {
			bool in_schema = false;
			for (int j = 0; j < schema_len; j++) {
				if (strcmp(column_name, schema[j]) == 0) {
					in_schema = true;
					break;
				}
			}
			if (!in_schema)
				continue;
		}
		SchemaColumn col;
		col.name   = strdup(column_name);
		col.reader = row_group_reader->Column(i);
		schema_vec.push_back(col);
	}
	return schema_vec;
}

static parquet_data_packet **
read_column_data(shared_ptr<parquet::ColumnReader> column_reader,
    const vector<int> &index_vector, int64_t batch_size,
    int &total_values_read)
{
    auto ba_reader =
        dynamic_pointer_cast<parquet::ByteArrayReader>(column_reader);
    if (!ba_reader->HasNext()) {
        log_error("Next is NULL");
        return nullptr;
    }

    ba_reader->Skip(index_vector[0]);
    vector<parquet_data_packet *> ret_vec;
    while (total_values_read < batch_size) {
        vector<int16_t>            def_levels(batch_size);
        vector<int16_t>            rep_levels(batch_size);
        vector<parquet::ByteArray> values(batch_size);
        int64_t                    values_read = 0;
        int64_t                    rows_read =
            ba_reader->ReadBatch(batch_size, def_levels.data(),
                rep_levels.data(), values.data(), &values_read);

        // No more rows; avoid infinite loop
        if (rows_read == 0) {
            break;
        }

        for (int64_t r = 0, i = 0; r < rows_read; r++) {

            if (def_levels[r] == 0) { // NULL value
                log_trace("Row %lld is NULL", r);
                ret_vec.push_back(nullptr);
            } else {
                parquet_data_packet *pack =
                    (parquet_data_packet *) malloc(
                        sizeof(parquet_data_packet));
                if (!pack) {
                    log_error("Memory allocation failed for parquet_data_packet");
                    for (auto p : ret_vec) {
                        if (p) {
                            free(p->data);
                            free(p);
                        }
                    }
                    return nullptr;
                }
                pack->data = (uint8_t *) malloc(values[i].len * sizeof(uint8_t));
                memcpy(pack->data, values[i].ptr, values[i].len);
                pack->size = values[i++].len;
                ret_vec.push_back(pack);
            }

            total_values_read += 1; // advance for every row, including NULLs

            if (batch_size == total_values_read) {
                parquet_data_packet **payload_arr = nullptr;
                if (!ret_vec.empty()) {
                    payload_arr =
                        (parquet_data_packet **) malloc(
                            sizeof(parquet_data_packet *) *
                            ret_vec.size());
                    copy(ret_vec.begin(), ret_vec.end(),
                        payload_arr);
                }
                return payload_arr;
            }
        }
    }

    // Early exit without fulfilling batch_size: free partial buffers and return nullptr
    for (auto p : ret_vec) {
        if (p) {
            free(p->data);
            free(p);
        }
    }

    return nullptr;
}

static vector<SchemaColumn>
get_all_schema_except_ts(shared_ptr<parquet::RowGroupReader> row_group_reader,
    shared_ptr<parquet::FileMetaData> file_metadata)
{
	vector<SchemaColumn> schema_vec;
	int num_columns = file_metadata->num_columns();
	for (int i = 1; i < num_columns; i++) {
		SchemaColumn col;
		col.name = strdup(file_metadata->schema()->Column(i)->name().c_str());
		col.reader = row_group_reader->Column(i);
		schema_vec.push_back(col);
	}
	return schema_vec;
}

static parquet_data_ret *parquet_read_payload(shared_ptr<parquet::RowGroupReader> row_group_reader, 
                                              shared_ptr<parquet::FileMetaData> file_metadata, 
                                              const char **schema, 
                                              uint16_t schema_len, 
                                              vector<int> &index_vector) {
    parquet_data_ret *ret = nullptr;
    vector<SchemaColumn> schema_vec;
    if (schema_len > 0) {
        schema_vec = get_filtered_schema(row_group_reader, file_metadata, schema, schema_len);
    } else {
        //all schema, do not include ts column
        log_warn("schema len <= 0 get all schema don't include ts column");
        schema_vec = get_all_schema_except_ts(row_group_reader, file_metadata);
    }
    vector<parquet_data_packet **> ret_rows_vec;
    int64_t batch_size = index_vector[1] - index_vector[0] + 1;

    for (const auto &col : schema_vec) {
        int total_values_read = 0;
        parquet_data_packet **payload_arr = read_column_data(col.reader, index_vector, batch_size, total_values_read);
        if (payload_arr) {
            ret_rows_vec.push_back(payload_arr);
        } else {
            ret_rows_vec.push_back(nullptr);
        }
    }

    if (!schema_vec.empty()) {
        ret = (parquet_data_ret *)malloc(sizeof(parquet_data_ret));
        if (!ret) {
            log_error("malloc failed");
            return ret;
        }
        ret->col_len = schema_vec.size();
        ret->payload_arr = (parquet_data_packet ***)malloc(sizeof(parquet_data_packet **) * ret->col_len);
        ret->schema = (char **)malloc(sizeof(char *) * ret->col_len);
        for (size_t i = 0; i < schema_vec.size(); ++i) {
            ret->schema[i] = schema_vec[i].name;
        }
        ret->row_len = batch_size;
        copy(ret_rows_vec.begin(), ret_rows_vec.end(), ret->payload_arr);
    }

    return ret;
}

static parquet_data_ret *
parquet_read_span_by_column(conf_parquet *conf, const char *filename, uint64_t keys[2],
    const char **schema, uint16_t schema_len)
{
	parquet_data_ret         *ret  = NULL;
	parquet::ReaderProperties reader_properties =
	    parquet::default_reader_properties();

	parquet_read_set_property(reader_properties, conf);
	vector<int> index_vector(2);

	// Create a ParquetReader instance
	string exception_msg = "";
	try {
		unique_ptr<parquet::ParquetFileReader> parquet_reader =
		    parquet::ParquetFileReader::OpenFile(
		        filename, false, reader_properties);

		// Get the File MetaData
		shared_ptr<parquet::FileMetaData> file_metadata =
		    parquet_reader->metadata();

		int num_row_groups =
		    file_metadata
		        ->num_row_groups(); // Get the number of RowGroups
		assert(num_row_groups == 1);

		for (int r = 0; r < num_row_groups; ++r) {

			vector<char *>                 schema_vec;
			vector<parquet_data_packet **> ret_rows_vec;
			vector<uint64_t>                ts;

			shared_ptr<parquet::RowGroupReader>
			    row_group_reader = parquet_reader->RowGroup(
			        r); // Get the RowGroup Reader
			shared_ptr<parquet::ColumnReader> column_reader;

			column_reader = row_group_reader->Column(0);

			parquet::Int64Reader *int64_reader =
			    static_cast<parquet::Int64Reader *>(
			        column_reader.get());

			index_vector = get_keys_indexes_fuzing(
			    int64_reader, ts, keys[0], keys[1]);
			if (-1 == index_vector[0] || -1 == index_vector[1]) {
				log_error("Not found data in key");
				return ret;
			}

			log_debug("start index: %lu, end index: %lu",
			    index_vector[0], index_vector[1]);
			ret = parquet_read_payload(row_group_reader,
			    file_metadata, schema, schema_len, index_vector);
			if (ret == NULL) {
				continue;
			}
			ret->ts =
			    (uint64_t *) malloc(sizeof(uint64_t) * ts.size());
			copy(ts.begin(), ts.end(), ret->ts);
		}

	} catch (const exception &e) {
		exception_msg = e.what();
		log_error("exception_msg=[%s]", exception_msg.c_str());
	}

	return ret;
}

typedef enum { START_KEY, END_KEY } key_type;

static uint64_t
get_key(const char *filename, key_type type)
{
	uint64_t range[2] = { 0 };
	uint64_t res      = 0;
	get_range(filename, range);
	switch (type) {
	case START_KEY:
		res = range[0];
		break;
	case END_KEY:
		res = range[1];
		break;
	default:
		break;
	}
	return res;
}

parquet_filename_range **
parquet_get_file_ranges(uint64_t start_key, uint64_t end_key, char *topic)
{
	uint32_t len = 0;
	// Find filenames
	log_info("topic: %s, start_key: %lu, end_key: %lu", topic, start_key, end_key);
	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	const char **filenames = parquet_find_span(topic, start_key, end_key, &len);
	vector<parquet_filename_range *> range_vec;

	// Get all keys
	for (uint32_t i = 0; i < len; i++) {
		log_info("filename: %s", filenames[i]);

		parquet_filename_range *range =
		    (parquet_filename_range *) nng_alloc(
		        sizeof(parquet_filename_range));
		range->keys[0] = start_key;
		range->keys[1] = end_key;
		if (len > 1) {
			range->keys[0] = i == 0
			    ? start_key
			    : get_key(filenames[i], START_KEY);
			range->keys[1] = i == (len - 1)
			    ? end_key
			    : get_key(filenames[i], END_KEY);
		}

		range->filename = filenames[i];

		log_debug("file start_key: %lu, file end_key: %lu",
		    range->keys[0], range->keys[1]);

		range_vec.push_back(range);
	}

	if (!range_vec.empty()) {
		// Push NULL as terminate
		range_vec.push_back(NULL);
		parquet_filename_range **ranges =
		    (parquet_filename_range **) nng_alloc(
		        sizeof(parquet_filename_range) * range_vec.size());
		copy(range_vec.begin(), range_vec.end(), ranges);

		nng_free(filenames, len);
		return ranges;
	}

	nng_free(filenames, len);
	return NULL;
}

bool
parquet_get_key_span(
    const char **topicl, uint32_t sz, uint64_t **key_span, uint64_t **sums)
{
	*key_span = (uint64_t *) nng_alloc(sz * 2 * sizeof(uint64_t));
	*sums     = (uint64_t *) nng_alloc(sz * sizeof(uint64_t));
	if (!*key_span || !*sums) {
		if (*key_span)
			nng_free(*key_span, sz * 2 * sizeof(uint64_t));
		if (*sums)
			nng_free(*sums, sz * sizeof(uint64_t));
		log_error("malloc memory failed!");
		return false; // allocation failed
	}
	void *elem = NULL;
	memset(*key_span, 0, sz * 2 * sizeof(uint64_t));
	memset(*sums, 0, sz * sizeof(uint64_t));

	pthread_mutex_lock(&parquet_queue_mutex);

	for (int idx = 0; idx < (int) sz; ++idx) {
		char    *first_file       = NULL;
		char    *last_file        = NULL;
		uint64_t file_key_span[2] = { 0 };
		auto     queue = file_manager.fetch_queue(topicl[idx]);

		if (NULL != queue) {

			FOREACH_QUEUE(*queue, elem)
			{
				if (elem) {
					if (!first_file) // Only set at the
					                 // first time
						first_file = (char *) elem;
					last_file = (char *) elem;
				}
			}
			if (first_file) {
				get_range(first_file, (*key_span) + 2 * idx);
			}
			if (last_file) {
				get_range(last_file, file_key_span);
			}
			(*key_span)[2 * idx + 1] = file_key_span[1];
			(*sums)[idx] = file_manager.get_queue_sum(topicl[idx]);
		}
	}

	pthread_mutex_unlock(&parquet_queue_mutex);
	return true;
}

void
parquet_free_key_span(uint64_t *key_span, uint64_t *sums, uint32_t sz)
{
	if (key_span) {
		nng_free(key_span, sz * 2 * sizeof(uint64_t));
	}
	if (sums) {
		nng_free(sums, sz * sizeof(uint64_t));
	}
}

parquet_data_ret **
parquet_get_data_packets_in_range_by_column(parquet_filename_range *range,
    const char *topic, const char **schema, uint16_t schema_len,
    uint32_t *size)
{

	conf_parquet *conf = file_manager.fetch_conf(topic);
	if (conf->enable == false) {
		log_error("Parquet %s is not ready or not launch!", topic);
		return NULL;
	}

	if (!range || !size) {
		log_error("range && size should not be NULL");
		return NULL;
	}

	parquet_data_ret **rets = NULL;

	if (range->filename) {
		// Search only one file
		parquet_data_ret *ret = parquet_read_span_by_column(conf, 
		    range->filename, range->keys, schema, schema_len);
		if (ret) {
			rets = (parquet_data_ret **) nng_alloc(
			    sizeof(parquet_data_ret *) * 1);
			log_debug("read span size: 1");
			*size = 1;
			*rets = ret;
		} else {
			return NULL;
		}

	} else {
		// Search multiple files
		vector<parquet_data_ret *> ret_vec;
		uint32_t                   len       = 0;
		uint64_t                   start_key = range->keys[0];
		uint64_t                   end_key   = range->keys[1];

		log_info("topic: %s, start_key: %lu, end_key: %lu", topic, start_key, end_key);
		const char **filenames =
		    parquet_find_span(topic, start_key, end_key, &len);

		for (uint32_t i = 0; i < len; i++) {
			log_info("filename: %s", filenames[i]);

			uint64_t keys[2];
			keys[0] = start_key;
			keys[1] = end_key;
			if (len > 1) {
				keys[0] = i == 0
				    ? start_key
				    : get_key(filenames[i], START_KEY);
				keys[1] = i == (len - 1)
				    ? end_key
				    : get_key(filenames[i], END_KEY);
			}

			log_debug("file start_key: %lu, file end_key: %lu",
			    keys[0], keys[1]);

			auto ret = parquet_read_span_by_column(conf,
			    filenames[i], keys, schema, schema_len);

			ret_vec.push_back(ret);
			nng_strfree((char *) filenames[i]);
		}
		nng_free(filenames, len);

		if (!ret_vec.empty()) {
			rets = (parquet_data_ret **) nng_alloc(
			    sizeof(parquet_data_ret *) * ret_vec.size());
			copy(ret_vec.begin(), ret_vec.end(), rets);
			*size = ret_vec.size();
		}
	}

	return rets;
}
