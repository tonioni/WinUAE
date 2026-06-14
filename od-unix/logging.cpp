#include "sysconfig.h"
#include "sysdeps.h"

#include "uae.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <mutex>
#include <string>
#include <vector>

int console_logging;
int always_flush_log;
TCHAR *conlogfile;
FILE *debugfile;

static constexpr size_t LOG_CAPTURE_LIMIT = 256 * 1024;

static std::mutex log_capture_mutex;
static std::string log_capture;

static void capture_log_bytes(const char *text, size_t len)
{
    if (!text || len == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(log_capture_mutex);
    if (len >= LOG_CAPTURE_LIMIT) {
        log_capture.assign(text + len - LOG_CAPTURE_LIMIT, LOG_CAPTURE_LIMIT);
        return;
    }
    if (log_capture.size() + len > LOG_CAPTURE_LIMIT) {
        log_capture.erase(0, log_capture.size() + len - LOG_CAPTURE_LIMIT);
    }
    log_capture.append(text, len);
}

static void capture_log_format(const char *format, va_list ap)
{
    va_list size_args;
    va_copy(size_args, ap);
    const int needed = vsnprintf(NULL, 0, format, size_args);
    va_end(size_args);
    if (needed <= 0) {
        return;
    }

    std::vector<char> buffer(size_t(needed) + 1);
    va_list format_args;
    va_copy(format_args, ap);
    vsnprintf(buffer.data(), buffer.size(), format, format_args);
    va_end(format_args);
    capture_log_bytes(buffer.data(), size_t(needed));
}

static void vlog_write(const char *format, va_list ap)
{
    va_list stderr_args;
    va_copy(stderr_args, ap);
    vfprintf(stderr, format, stderr_args);
    va_end(stderr_args);
    fflush(stderr);

    if (debugfile) {
        va_list file_args;
        va_copy(file_args, ap);
        vfprintf(debugfile, format, file_args);
        va_end(file_args);
        if (always_flush_log) {
            fflush(debugfile);
        }
    }

    capture_log_format(format, ap);
}

void write_log(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog_write(format, ap);
    va_end(ap);
}

void write_logx(const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog_write(format, ap);
    va_end(ap);
}

void write_dlog(const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog_write(format, ap);
    va_end(ap);
}

int read_log(void)
{
    return 0;
}

void flush_log(void)
{
    fflush(stderr);
    if (debugfile) {
        fflush(debugfile);
    }
}

void logging_init(void)
{
}

uae_u8 *save_log(int, size_t *len)
{
    if (!len) {
        return NULL;
    }

    flush_log();

    std::lock_guard<std::mutex> lock(log_capture_mutex);
    size_t size = log_capture.size();
    size_t offset = 0;
    if (*len > 0 && size > *len) {
        offset = size - *len;
        size = *len;
    }
    if (size == 0) {
        *len = 0;
        return NULL;
    }

    uae_u8 *dst = xmalloc(uae_u8, size + 1);
    if (!dst) {
        *len = 0;
        return NULL;
    }
    memcpy(dst, log_capture.data() + offset, size);
    dst[size] = 0;
    *len = size + 1;
    return dst;
}

FILE *log_open(const TCHAR *name, int append, int, TCHAR*)
{
    return fopen(name, append ? "ab" : "wb");
}

void log_close(FILE *f)
{
    if (f) {
        if (f == debugfile) {
            debugfile = NULL;
        }
        fclose(f);
    }
}

TCHAR *setconsolemode(TCHAR *buffer, int)
{
    return buffer;
}

void close_console(void) {}
void open_console(void) {}
bool is_interactive_console(void) { return true; }
void reopen_console(void) {}
void activate_console(void) {}
void deactivate_console(void) {}
void set_console_input_mode(int) {}
bool is_console_open(void) { return true; }
void console_out(const TCHAR *s)
{
    fputs(s, stderr);
    if (debugfile) {
        fputs(s, debugfile);
        if (always_flush_log) {
            fflush(debugfile);
        }
    }
    capture_log_bytes(s, strlen(s));
}
void console_out_f(const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog_write(format, ap);
    va_end(ap);
}
void console_flush(void) { flush_log(); }
int console_get(TCHAR *, int) { return 0; }
bool console_isch(void) { return false; }
TCHAR console_getch(void) { return 0; }

void jit_abort(const char *format, ...)
{
    char buffer[4096];

    va_list ap;
    va_start(ap, format);
    vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    buffer[sizeof(buffer) - 1] = 0;

    write_log("JIT: Serious error: %s\n", buffer);
    uae_reset(1, 0);
}

void f_out(void *, const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlog_write(format, ap);
    va_end(ap);
}
TCHAR* buf_out(TCHAR *buffer, int *bufsize, const TCHAR *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int n = vsnprintf(buffer, *bufsize, format, ap);
    va_end(ap);
    if (n >= 0 && n < *bufsize) {
        *bufsize -= n;
        return buffer + n;
    }
    return buffer;
}
TCHAR *write_log_get_ts(void)
{
    static TCHAR ts[1] = { 0 };
    return ts;
}
