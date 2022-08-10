#include "nng/supplemental/nanolib/log.h"
#include "core/nng_impl.h"
#include "core/defs.h"

#if defined(SUPP_SYSLOG)
#include <syslog.h>
#endif

#define MAX_CALLBACKS 10

typedef struct {
	log_func fn;
	void *   udata;
	uint8_t  level;
	nng_mtx *mtx;
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

static void
stdout_callback(log_event *ev)
{
	char buf[64];
	buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
#ifdef LOG_USE_COLOR
	fprintf(ev->udata,
	    "%s [%i] %s%-5s\x1b[0m \x1b[90m%s:%d \x1b[0m %s: ", buf,
	    nni_plat_getpid(), level_colors[ev->level],
	    level_strings[ev->level], ev->file, ev->line, ev->func);
#else
	fprintf(ev->udata, "%s [%i] %-5s %s:%d %s: ", buf, nni_plat_getpid(),
	    level_strings[ev->level], ev->file, ev->line, ev->func);
#endif
	vfprintf(ev->udata, ev->fmt, ev->ap);
	fprintf(ev->udata, "\n");
	fflush(ev->udata);
}

static void
file_callback(log_event *ev)
{
	char buf[64];
	buf[strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", ev->time)] = '\0';
	fprintf(ev->udata, "%s [%i] %-5s %s:%d: ", buf, nni_plat_getpid(),
	    level_strings[ev->level], ev->file, ev->line);
	vfprintf(ev->udata, ev->fmt, ev->ap);
	fprintf(ev->udata, "\n");
	fflush(ev->udata);
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
	syslog(ev->level, "%s: %s", ev->fmt, ev->func);
}

void
log_add_syslog(const char *log_name, uint8_t level, void *mtx)
{
	openlog(log_name, LOG_PID, LOG_DAEMON | convert_syslog_level(level));
	log_add_callback(syslog_callback, NULL, level, mtx);
}
#else

void
log_add_syslog(const char *log_name, uint8_t level, void *mtx)
{
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

void
log_set_level(int level)
{
	L.level = level;
}

int
log_add_callback(log_func fn, void *udata, int level, void *mtx)
{
	for (int i = 0; i < MAX_CALLBACKS; i++) {
		if (!L.callbacks[i].fn) {
			L.callbacks[i] = (log_callback) {
				.fn    = fn,
				.udata = udata,
				.level = level,
				.mtx   = (nng_mtx *) mtx,
			};
			return 0;
		}
	}
	return -1;
}

int
log_add_fp(FILE *fp, int level, void *mtx)
{
	return log_add_callback(file_callback, fp, level, mtx);
}

void
log_add_console(int level, void *mtx)
{
	log_add_callback(stdout_callback, stdout, level, mtx);
}

static void
init_event(log_event *ev, void *udata)
{
	if (!ev->time) {
		time_t t = time(NULL);
		ev->time = localtime(&t);
	}
	ev->udata = udata;
}

void
log_log(int level, const char *file, int line, const char *func,
    const char *fmt, ...)
{
	const char *file_name = file;

	log_event ev = {
		.fmt   = fmt,
		.file  = file_name,
		.line  = line,
		.level = level,
		.func  = func,
	};

	for (int i = 0; i < MAX_CALLBACKS && L.callbacks[i].fn; i++) {
		log_callback *cb = &L.callbacks[i];
		if (level <= cb->level) {
			init_event(&ev, cb->udata);
			va_start(ev.ap, fmt);
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
}
