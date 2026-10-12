#include "nng/supplemental/nanolib/log.h"
#include "nng/supplemental/nanolib/conf.h"
#include "nng/supplemental/nanolib/file.h"
#include "core/nng_impl.h"
#include "core/defs.h"

#define INDEX_FILE_NAME ".idx"
#define MAX_CALLBACKS 10

#if NNG_PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#define ssize_t int  /* Windows doesn't have ssize_t */
#define nano_localtime(t, pTm) localtime_s(pTm, t)
#else
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <errno.h>
#if defined(SUPP_SYSLOG)
#include <syslog.h>
static int syslog_socket = -1;
static struct sockaddr_un syslog_addr;
static const char *log_ident = NULL;
static int log_option = 0;
static int log_facility = LOG_USER;
#endif
#define nano_localtime(t, pTm) localtime_r(t, pTm)
#endif


typedef struct {
	log_func  fn;
	void *    udata;
	uint8_t   level;
	bool      is_trace; // a protocol-trace sink, filtered by category
	nng_mtx * mtx;
	conf_log *config;
} log_callback;

static struct {
	void *       udata;
	uint8_t      level;
	log_callback callbacks[MAX_CALLBACKS];
} L;

static const char *level_strings[] = {
	"FATAL",
	"ERROR",
	"WARN",
	"INFO",
	"DEBUG",
	"TRACE",
};

static uint32_t trace_mask = 0;

static size_t trace_payload_limit = NMQ_TRACE_PAYLOAD_LIMIT_DEFAULT;

static const struct {
	uint32_t    bit;
	const char *name;
} trace_categories[] = {
	{ NMQ_TRACE_CONNECT, "connect" },
	{ NMQ_TRACE_AUTH, "auth" },
	{ NMQ_TRACE_SUB, "sub" },
	{ NMQ_TRACE_PUB, "pub" },
	{ NMQ_TRACE_PAYLOAD, "payload" },
	{ NMQ_TRACE_SESSION, "session" },
	{ NMQ_TRACE_TLS, "tls" },
	{ NMQ_TRACE_ACL, "acl" },
	{ 0, NULL },
};

#ifdef LOG_USE_COLOR
static const char *level_colors[] = {
	"\x1b[35m",
	"\x1b[31m",
	"\x1b[33m",
	"\x1b[32m",
	"\x1b[36m",
	"\x1b[94m",
};
#endif

static void file_rotation(FILE *fp, conf_log *config);

#if defined(_MSC_VER)
#define NMQ_THREAD_LOCAL __declspec(thread)
#else
#define NMQ_THREAD_LOCAL __thread
#endif

// Records inside one second all carry the same 19 characters, so rendering the
// timestamp per record spends a localtime_r() and an strftime() to produce
// bytes the thread already has. Thread local, so no two worker threads share
// the buffer and no lock guards it.
static NMQ_THREAD_LOCAL struct {
	time_t sec;
	char   text[20]; // "YYYY-MM-DD HH:MM:SS"
	int    records;  // records written in this second, for the flush policy
} trace_clock;

// True when this record is the first of a new second on this thread, which is
// what the trace sink flushes on.
static bool
trace_clock_tick(void)
{
	const time_t now = time(NULL);
	struct tm    broken;

	if (now == trace_clock.sec) {
		trace_clock.records++;
		return false;
	}
	trace_clock.sec     = now;
	trace_clock.records = 1;
	nano_localtime(&now, &broken);
	strftime(trace_clock.text, sizeof(trace_clock.text),
	    "%Y-%m-%d %H:%M:%S", &broken);
	return true;
}

// gettid() is a syscall, and the answer cannot change for the life of the
// thread, so a trace sink asks the kernel once per thread rather than once per
// record.
static long
trace_thread_id(void)
{
#if (NNG_PLATFORM_WINDOWS || NNG_PLATFORM_DARWIN)
	return (long) nni_plat_getpid();
#else
	static NMQ_THREAD_LOCAL long tid = 0;

	if (tid == 0) {
		tid = (long) syscall(__NR_gettid);
	}
	return tid;
#endif
}

// A trace sink is written to by the worker thread that handled the packet, and
// an fflush() per record is a write(2) per message. Records are flushed when
// the second rolls over instead, which bounds how far behind a reader can be
// at one second. The first few records of each second are flushed as they are
// written, so tracing a quiet broker still shows the record immediately and
// only a rate high enough to make the syscall hurt defers anything.
#define NMQ_TRACE_FLUSH_BURST 16

static bool
trace_should_flush(bool second_rolled)
{
	return second_rolled || trace_clock.records <= NMQ_TRACE_FLUSH_BURST;
}

// stdio defaults to a 4KB buffer, so even with the flush policy above a trace
// sink taking a record per message would still write(2) every few records. A
// buffer this size turns a busy second into a handful of writes.
#define NMQ_TRACE_BUF_SZ (256 * 1024)

// Must be called before anything is written to the stream, so every place that
// makes a trace file current calls it on the way in.
static void
trace_file_buffer(FILE *fp)
{
	if (fp != NULL) {
		setvbuf(fp, NULL, _IOFBF, NMQ_TRACE_BUF_SZ);
	}
}

// The rotation counter has to start at the size the file already has, since a
// trace file is opened for append and may hold the records of earlier runs.
// One stat() when the file becomes current, rather than one per record.
static void
trace_file_seed(conf_log *config)
{
	size_t sz = 0;

	config->bytes_written =
	    nni_plat_file_size(config->abs_path, &sz) == 0 ? (uint64_t) sz : 0;
}

// The payload is written as raw bytes, which is why it is the last field of a
// record and why payload_len sits in front of it: a reader frames the body by
// that length rather than by looking for the next newline. Escaping it would
// cost a branch per byte and up to four output bytes per input byte, and the
// escape buffer is what used to cap a traced payload at 1024 bytes.
static size_t
trace_write_data(FILE *fp, const void *data, size_t len)
{
	const size_t limit   = trace_payload_limit;
	size_t       shown   = (limit != 0 && len > limit) ? limit : len;
	size_t       written = 0;

	if (data == NULL || len == 0) {
		return 0;
	}
	written += fwrite(data, 1, shown, fp);
	if (shown < len) {
		written += fwrite(NMQ_TRACE_PAYLOAD_TRUNCATED, 1,
		    sizeof(NMQ_TRACE_PAYLOAD_TRUNCATED) - 1, fp);
	}
	return written;
}

// A trace sink can write a line per message, where the build-time absolute
// path is a third of the bytes and tells the reader nothing. Ordinary log
// lines keep the full path they have always had.
static const char *
event_source_file(const log_event *ev)
{
	const char *slash;

	if (ev->category == 0 || ev->file == NULL) {
		return ev->file;
	}
	slash = strrchr(ev->file, '/');
	return slash != NULL ? slash + 1 : ev->file;
}

static void
stdout_callback(log_event *ev)
{
	char buf[64];
#if (NNG_PLATFORM_WINDOWS || NNG_PLATFORM_DARWIN)
	int pid = nni_plat_getpid();
#else
	pid_t pid = syscall(__NR_gettid);
#endif

	const char *tag = ev->category == 0
	    ? level_strings[ev->level]
	    : log_trace_category_string(ev->category);
	int tag_width = ev->category == 0 ? 5 : 7;

	buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ev->time)] = '\0';
#ifdef LOG_USE_COLOR
	fprintf(ev->udata,
	    "%s [%i] %s%-*s\x1b[0m \x1b[0m%s:%d \x1b[0m %s: ", buf, pid,
	    level_colors[ev->level], tag_width, tag, event_source_file(ev),
	    ev->line, ev->func);
#else
	fprintf(ev->udata, "%s [%i] %-*s %s:%d %s: ", buf, pid,
	    tag_width, tag, event_source_file(ev), ev->line, ev->func);
#endif
	vfprintf(ev->udata, ev->fmt, ev->ap);
	trace_write_data(ev->udata, ev->data, ev->data_len);
	fprintf(ev->udata, "\n");
	fflush(ev->udata);
}

static void
file_callback(log_event *ev)
{
	char buf[64];
#if (NNG_PLATFORM_WINDOWS || NNG_PLATFORM_DARWIN)
	int pid = nni_plat_getpid();
#else
	pid_t pid = syscall(__NR_gettid);
#endif
#ifndef NNG_PLATFORM_WINDOWS
	if (nng_access(ev->config->dir, W_OK) < 0) {
		fprintf(stderr, "open path %s failed! close file!\n",
				ev->config->dir);
		if (ev->config->fp != NULL)
			fclose(ev->config->fp);
		ev->config->fp = NULL;
		return;
	}
#endif
	if (ev->config->fp == NULL)
		ev->config->fp = fopen(ev->config->abs_path, "a");
	FILE *fp = ev->config->fp;
	buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ev->time)] = '\0';
	fprintf(fp, "%s [%i] %-5s %s:%d: ", buf, pid,
	    level_strings[ev->level], ev->file, ev->line);
	vfprintf(fp, ev->fmt, ev->ap);
	fprintf(fp, "\n");
	fflush(fp);

	file_rotation(fp, ev->config);
}

// The trace file sink is deliberately not file_callback. An ordinary log line
// is rare enough that its per-line access(), stat() and fflush() cost nothing
// worth counting, and they buy durability that an operator reading nanomq.log
// after a crash wants. A trace record is written per message, so this sink pays
// those three syscalls per second and per rotation instead of per record: the
// directory is checked when the file is opened, rotation is driven by a byte
// counter, and the flush policy is trace_should_flush(). Nothing here changes
// how nanomq.log is written.
static void
trace_file_callback(log_event *ev)
{
	conf_log *config = ev->config;
	FILE *    fp     = config->fp;
	bool      rolled = trace_clock_tick();
	int       n;

	if (fp == NULL) {
#ifndef NNG_PLATFORM_WINDOWS
		if (nng_access(config->dir, W_OK) < 0) {
			fprintf(stderr, "open path %s failed! close file!\n",
			    config->dir);
			return;
		}
#endif
		if ((fp = fopen(config->abs_path, "a")) == NULL) {
			fprintf(stderr, "open trace file %s failed\n",
			    config->abs_path);
			return;
		}
		trace_file_buffer(fp);
		trace_file_seed(config);
		config->fp = fp;
	}

	n = fprintf(fp, "%s [%ld] %-7s %s:%d: ", trace_clock.text,
	    trace_thread_id(), log_trace_category_string(ev->category),
	    event_source_file(ev), ev->line);
	if (n > 0) {
		config->bytes_written += (uint64_t) n;
	}
	n = vfprintf(fp, ev->fmt, ev->ap);
	if (n > 0) {
		config->bytes_written += (uint64_t) n;
	}
	config->bytes_written += trace_write_data(fp, ev->data, ev->data_len);
	fputc('\n', fp);
	config->bytes_written++;

	if (trace_should_flush(rolled)) {
		fflush(fp);
	}

	if (config->bytes_written >= config->rotation_sz) {
		fflush(fp);
		file_rotation(fp, config);
		// file_rotation() renames and reopens, so the file to buffer
		// and to count from is whichever one is current afterwards.
		trace_file_buffer(config->fp);
		trace_file_seed(config);
	}
}

#if defined(SUPP_SYSLOG)

static uint8_t
convert_syslog_level(uint8_t level)
{
	switch (level) {
	case NNG_LOG_FATAL:
		return LOG_EMERG;
	case NNG_LOG_ERROR:
		return LOG_ERR;
	case NNG_LOG_WARN:
		return LOG_WARNING;
	case NNG_LOG_INFO:
		return LOG_INFO;
	case NNG_LOG_DEBUG:
	case NNG_LOG_TRACE:
		return LOG_DEBUG;
	default:
		return LOG_WARNING;
	}
}

static void
syslog_callback(log_event *ev)
{

#if (NNG_PLATFORM_WINDOWS || NNG_PLATFORM_DARWIN)
	int pid = nni_plat_getpid();
#else
	pid_t pid = syscall(__NR_gettid);
#endif

	// Create buffer for the log prefix
	char buf[256];
	snprintf(buf, sizeof(buf), "[%i] %-5s %s:%d %s: ", pid,
	    level_strings[ev->level], ev->file, ev->line, ev->func);

	// Concatenate buf and ev->fmt
	char final_fmt[512]; // Adjust size as needed
	snprintf(final_fmt, sizeof(final_fmt), "%s%s", buf, ev->fmt);

	vsyslog(ev->level, ev->fmt, ev->ap);
}

void
log_add_syslog(const char *log_name, uint8_t level, void *mtx)
{
	openlog(log_name, LOG_PID, LOG_DAEMON | convert_syslog_level(level));
	log_add_callback(syslog_callback, NULL, level, mtx, NULL);
}
#ifndef NNG_PLATFORM_WINDOWS
void
uds_openlog(const char *uds_path, const char *ident, int option, int facility)
{
	log_ident    = ident;
	log_option   = option;
	log_facility = facility;

	if ((syslog_socket = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
		fprintf(stderr, "socket %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	memset(&syslog_addr, 0, sizeof(syslog_addr));
	syslog_addr.sun_family = AF_UNIX;
	strncpy(
	    syslog_addr.sun_path, uds_path, sizeof(syslog_addr.sun_path) - 1);

	int len = offsetof(struct sockaddr_un, sun_path) + strlen(uds_path);
	if (connect(syslog_socket, (struct sockaddr *) &syslog_addr, len) <
	    0) {
		fprintf(stderr, "connect to %s %s", uds_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void
uds_vsyslog(int priority, const char *format, va_list args)
{
	char message[1024];
	char final_message[1064];

	vsnprintf(message, sizeof(message), format, args);

	if (log_ident) {
		snprintf(final_message, sizeof(final_message), "<%d>%s: %s",
		    priority | log_facility, log_ident, message);
	} else {
		snprintf(final_message, sizeof(final_message), "<%d>%s",
		    priority | log_facility, message);
	}

	size_t message_len = strlen(final_message);

	if (syslog_socket >= 0) {
		if (sendto(syslog_socket, final_message, message_len, 0,
		        (struct sockaddr *) &syslog_addr,
		        sizeof(syslog_addr)) < 0) {
			perror("sendto");
		}
	} else {
		fprintf(stderr, "Syslog socket not open\n");
	}
}

void uds_syslog(int priority, const char *format, ...) {
    va_list args;
    va_start(args, format);
    uds_vsyslog(priority, format, args);
    va_end(args);
}

void uds_closelog(void) {
    if (syslog_socket >= 0) {
        close(syslog_socket);
        syslog_socket = -1;
    }
}

static void
uds_syslog_callback(log_event *ev)
{

#if (NNG_PLATFORM_WINDOWS || NNG_PLATFORM_DARWIN)
	int pid = nni_plat_getpid();
#else
	pid_t pid = syscall(__NR_gettid);
#endif

	// Create buffer for the log prefix
	char buf[256];
	snprintf(buf, sizeof(buf), "[%i] %-5s %s:%d %s: ", pid,
	    level_strings[ev->level], ev->file, ev->line, ev->func);

	// Concatenate buf and ev->fmt
	char final_fmt[512]; // Adjust size as needed
	snprintf(final_fmt, sizeof(final_fmt), "%s%s", buf, ev->fmt);

	// Pass the modified format string to uds_vsyslog
	uds_vsyslog(ev->level, final_fmt, ev->ap);
}

void
log_add_uds(const char *uds_path, const char *log_name, uint8_t level, void *mtx)
{
	uds_openlog(uds_path, log_name, LOG_PID, LOG_DAEMON | convert_syslog_level(level));
	log_add_callback(uds_syslog_callback, NULL, level, mtx, NULL);
}
#endif

#else

void
log_add_syslog(const char *log_name, uint8_t level, void *mtx)
{
	NNI_ARG_UNUSED(log_name);
	NNI_ARG_UNUSED(level);
	NNI_ARG_UNUSED(mtx);
}

void
log_add_uds(const char *uds_path, const char *log_name, uint8_t level, void *mtx)
{
	NNI_ARG_UNUSED(uds_path);
	NNI_ARG_UNUSED(log_name);
	NNI_ARG_UNUSED(level);
	NNI_ARG_UNUSED(mtx);
}

#endif


const char *
log_level_string(int level)
{
	return level_strings[level];
}

int
log_level_num(const char *level)
{
	int count = (int) (sizeof(level_strings) / sizeof(level_strings[0]));
	for (int i = 0; i < count; i++) {
		if (nni_strcasecmp(level, level_strings[i]) == 0) {
			return i;
		}
	}
	return -1;
}

uint32_t
log_trace_category_num(const char *name)
{
	if (name == NULL) {
		return 0;
	}
	if (nni_strcasecmp(name, "all") == 0) {
		return NMQ_TRACE_ALL;
	}
	for (int i = 0; trace_categories[i].name != NULL; i++) {
		if (nni_strcasecmp(name, trace_categories[i].name) == 0) {
			return trace_categories[i].bit;
		}
	}
	return 0;
}

const char *
log_trace_category_string(uint32_t category)
{
	for (int i = 0; trace_categories[i].name != NULL; i++) {
		if (trace_categories[i].bit == category) {
			return trace_categories[i].name;
		}
	}
	return "trace";
}

void
log_trace_categories_string(uint32_t categories, char *out, size_t out_sz)
{
	size_t used = 0;

	if (out == NULL || out_sz == 0) {
		return;
	}
	out[0] = '\0';
	for (int i = 0; trace_categories[i].name != NULL; i++) {
		size_t name_len;
		if (0 == (categories & trace_categories[i].bit)) {
			continue;
		}
		name_len = strlen(trace_categories[i].name);
		if (used + name_len + 2 > out_sz) {
			break;
		}
		if (used > 0) {
			out[used++] = ',';
		}
		memcpy(out + used, trace_categories[i].name, name_len);
		used += name_len;
	}
	if (used == 0) {
		snprintf(out, out_sz, "none");
		return;
	}
	out[used] = '\0';
}

uint32_t
log_trace_categories_parse(const char *list)
{
	uint32_t    categories = 0;
	const char *p          = list;

	if (list == NULL) {
		return 0;
	}
	while (*p != '\0') {
		char     name[32];
		size_t   n = 0;
		uint32_t bit;

		while (*p == ',' || *p == ' ' || *p == '\t') {
			p++;
		}
		while (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t') {
			if (n + 1 < sizeof(name)) {
				name[n++] = *p;
			}
			p++;
		}
		if (n == 0) {
			continue;
		}
		name[n] = '\0';
		if ((bit = log_trace_category_num(name)) == 0) {
			log_error("Unknown log trace category %s", name);
			continue;
		}
		categories |= bit;
	}
	return categories;
}

// For trace category hot update, driven by `nanomq reload`. The caller must
// have registered a trace sink first, otherwise the events are formatted and
// then dropped. Raced deliberately, as log_update_level() already races
// L.level.
void
log_trace_set_categories(uint32_t categories)
{
	trace_mask = categories;
}

uint32_t
log_trace_get_categories(void)
{
	return trace_mask;
}

void
log_trace_set_payload_limit(size_t limit)
{
	trace_payload_limit = limit;
}

// for log level hot update
void
log_update_level(int new_level)
{
    L.level = new_level;
    for (int i = 0; i < MAX_CALLBACKS; i++) {
		// let it race, modern CPU will take care of it.
        if (L.callbacks[i].fn)
			L.callbacks[i].level = new_level;
    }
}

void
log_set_level(int level)
{
	L.level = level;
}

static int
log_add_callback_cat(log_func fn, void *udata, int level, void *mtx,
    conf_log *config, bool is_trace)
{
	for (int i = 0; i < MAX_CALLBACKS; i++) {
		if (!L.callbacks[i].fn) {
			L.callbacks[i] = (log_callback) {
				.fn       = fn,
				.udata    = udata,
				.level    = level,
				.is_trace = is_trace,
				.mtx      = (nng_mtx *) mtx,
				.config   = config,
			};
			return 0;
		}
	}
	return -1;
}

int
log_add_callback(
    log_func fn, void *udata, int level, void *mtx, conf_log *config)
{
	return log_add_callback_cat(fn, udata, level, mtx, config, false);
}

void
log_clear_callback()
{
	memset(L.callbacks, 0, sizeof(log_callback) * MAX_CALLBACKS);
}

static void
file_rotation(FILE *fp, conf_log *config)
{
	// Note : do not call log_xxx() in this function, it will cause dead lock
	size_t sz = 0;
	int    rv;
	if ((rv = nni_plat_file_size(config->abs_path, &sz)) != 0) {
		fprintf(stderr, "get file %s size failed: %s\n",
		    config->abs_path, nng_strerror(rv));
		if (!nni_plat_file_exists(config->abs_path)) {
			// file missing, recreate one
			if (fp)
				fclose(fp);
#ifndef NNG_PLATFORM_WINDOWS
			if (nng_access(config->dir, W_OK) < 0) {
				fprintf(stderr, "open path %s failed\n",
						config->dir);
				config->fp = NULL;
				return;
			}
#endif
			if (nng_file_put(config->abs_path, "\n", 1) != 0) {
				fprintf(stderr, "write to file %s failed: %s\n",
				    config->abs_path, nng_strerror(rv));
				config->fp = NULL;
				return;
			}

			config->fp = fopen(config->abs_path, "a");
			fp             = config->fp;
		}
		return;
	}

	if (sz >= config->rotation_sz) {
		char *index_file =
		    nano_concat_path(config->dir, INDEX_FILE_NAME);
		char * index_data = NULL;
		size_t size       = 0;
		size_t index      = 1;
		char   buf[4]     = { 0 };

		if ((rv = nni_plat_file_get(
		         index_file, (void **) &index_data, &size)) == 0) {
			memcpy(buf, index_data, size);
			if (1 != sscanf(buf, "%zu", &index)) {
				index = 1;
			}
			nni_free(index_data, size);
		}

		size_t log_name_len = strlen(config->abs_path) + 20;
		char * log_name     = nni_zalloc(log_name_len);
		snprintf(
		    log_name, log_name_len, "%s.%lu", config->file, index);
		char *backup_log_path =
		    nano_concat_path(config->dir, log_name);
		if (fp)
			fclose(fp);
		fp = NULL;
		remove(backup_log_path);
		rename(config->abs_path, backup_log_path);
		nni_free(log_name, log_name_len);
		nni_strfree(backup_log_path);
#ifndef NNG_PLATFORM_WINDOWS
		if (nng_access(config->dir, W_OK) < 0) {
			fprintf(stderr, "open path %s failed\n",
					config->dir);
			config->fp = NULL;
			nni_strfree(index_file);
			return;
		}
#endif
		fp           = fopen(config->abs_path, "a");
		config->fp   = fp;
		char num[20] = { 0 };
		index++; // increase index
		if (index > config->rotation_count) {
			index = 1;
		}
		snprintf(num, 20, "%zu", index);
		if ((rv = nni_plat_file_put(index_file, num, strlen(num))) !=
		    0) {
			fprintf(stderr, "write to file %s failed: %s\n",
			    index_file, nng_strerror(rv));
		}
		nni_strfree(index_file);
	}
}

int
log_add_fp(FILE *fp, int level, void *mtx, conf_log *config)
{
	return log_add_callback(file_callback, fp, level, mtx, config);
}

void
log_add_console(int level, void *mtx)
{
	log_add_callback(stdout_callback, stdout, level, mtx, NULL);
}

// A trace sink is filtered by category rather than by level, so it is
// registered at NNG_LOG_TRACE and the level never gates it.
int
log_add_trace_fp(FILE *fp, void *mtx, conf_log *config)
{
	trace_file_buffer(fp);
	trace_file_seed(config);
	return log_add_callback_cat(
	    trace_file_callback, fp, NNG_LOG_TRACE, mtx, config, true);
}

void
log_add_trace_console(void *mtx)
{
	log_add_callback_cat(
	    stdout_callback, stdout, NNG_LOG_TRACE, mtx, NULL, true);
}

static void
init_event(log_event *ev, void *udata, conf_log *config)
{
	const time_t now_seconds = time(NULL);
	nano_localtime(&now_seconds, &ev->time);
	ev->udata  = udata;
	ev->config = config;
}

// An ordinary log event reaches the ordinary sinks and a trace event reaches
// the trace sinks, so raising log.level never floods the trace file and
// enabling a trace category never floods nanomq.log.
static void
log_emit(int level, uint32_t category, const char *file, int line,
    const char *func, const void *data, size_t data_len, const char *fmt,
    va_list ap)
{
	log_event ev = {
		.fmt      = fmt,
		.file     = file,
		.line     = line,
		.level    = level,
		.category = category,
		.func     = func,
		.data     = data,
		.data_len = data_len,
	};

	for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
		log_callback *cb = &L.callbacks[i];
		if (category == 0) {
			if (cb->is_trace || level > cb->level) {
				continue;
			}
		} else if (!cb->is_trace) {
			continue;
		}
		init_event(&ev, cb->udata, cb->config);
		va_copy(ev.ap, ap);
		if (cb->mtx == NULL) {
			cb->fn(&ev);
		} else {
			nng_mtx_lock(cb->mtx);
			cb->fn(&ev);
			nng_mtx_unlock(cb->mtx);
		}
		va_end(ev.ap);
	}
}

void
log_log(int level, const char *file, int line, const char *func,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_emit(level, 0, file, line, func, NULL, 0, fmt, ap);
	va_end(ap);
}

void
log_log_trace(uint32_t category, const char *file, int line, const char *func,
    const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_emit(NNG_LOG_TRACE, category, file, line, func, NULL, 0, fmt, ap);
	va_end(ap);
}

void
log_log_trace_data(uint32_t category, const char *file, int line,
    const char *func, const void *data, size_t data_len, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_emit(NNG_LOG_TRACE, category, file, line, func, data, data_len,
	    fmt, ap);
	va_end(ap);
}
