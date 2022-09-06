#ifndef NNG_NANOLIB_LOG_H
#define NNG_NANOLIB_LOG_H
#include "nng/nng.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define LOG_VERSION "0.2.0"

typedef struct {
	va_list         ap;
	const char *    fmt;
	const char *    file;
	const char *    func;
	struct tm       time;
	void *          udata;
	int             line;
	int             level;
} log_event;

typedef void (*log_func)(log_event *ev);

enum {
	NNG_LOG_FATAL = 0,
	NNG_LOG_ERROR,
	NNG_LOG_WARN,
	NNG_LOG_INFO,
	NNG_LOG_DEBUG,
	NNG_LOG_TRACE,
};

NNG_DECL const char *log_level_string(int level);
NNG_DECL int         log_level_num(const char *level);
NNG_DECL void        log_set_level(int level);
NNG_DECL int log_add_callback(log_func fn, void *udata, int level, void *mtx);
NNG_DECL void        log_add_console(int level, void *mtx);
NNG_DECL int         log_add_fp(FILE *fp, int level, void *mtx);
NNG_DECL void log_add_syslog(const char *log_name, uint8_t level, void *mtx);
NNG_DECL void log_log(int level, const char *file, int line, const char *func,
    const char *fmt, ...);

#ifdef ENABLE_LOG

#define log_trace(...) \
    log_log(NNG_LOG_TRACE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define log_debug(...) \
    log_log(NNG_LOG_DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define log_info(...) \
    log_log(NNG_LOG_INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define log_warn(...) \
    log_log(NNG_LOG_WARN, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define log_error(...) \
    log_log(NNG_LOG_ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define log_fatal(...) \
    log_log(NNG_LOG_FATAL, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

#else

#define log_trace(...)
#define log_debug(...)
#define log_info(...)
#define log_warn(...)
#define log_error(...)
#define log_fatal(...)

#endif


#ifdef __cplusplus
}
#endif

#endif