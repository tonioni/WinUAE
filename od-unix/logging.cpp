#include "sysconfig.h"
#include "sysdeps.h"

#include "debug.h"
#include "host.h"
#include "registry.h"
#include "uae.h"
#include "video.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <mutex>
#include <string>
#include <vector>

int console_logging;
int always_flush_log;
TCHAR *conlogfile;
FILE *debugfile;

extern int consoleopen;
extern int unix_gui_debugger_get_input(TCHAR *out, int maxlen);
extern void unix_gui_debugger_write(const TCHAR *text);
extern void unix_gui_debugger_close(void);

static constexpr size_t LOG_CAPTURE_LIMIT = 256 * 1024;

static std::mutex log_capture_mutex;
static std::string log_capture;
static TCHAR *console_buffer;
static int console_buffer_size;
static int debugger_type = -1;

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

static bool terminal_console_available(void)
{
    return isatty(STDIN_FILENO) != 0;
}

static bool gui_debugger_available(void)
{
#ifdef WINUAE_UNIX_WITH_INTEGRATED_QT_UI
    return true;
#else
    return false;
#endif
}

static void init_debugger_type(void)
{
    if (debugger_type >= 0) {
        return;
    }
    if (!regqueryint(NULL, _T("DebuggerType"), &debugger_type) || debugger_type <= 0) {
        // Integrated UI builds should open the Qt debugger unless the user
        // explicitly selected the terminal backend (DebuggerType=1).
        debugger_type = gui_debugger_available() ? 2 : 1;
    }
    if (debugger_type == 2 && !gui_debugger_available()) {
        debugger_type = 1;
    }
    if (debugger_type == 1 && !terminal_console_available() && gui_debugger_available()) {
        debugger_type = 2;
    }
}

static bool poll_host_window_events(void)
{
    unix_host_check_quit();
    bool quit_requested = false;
    unix_video_poll_window_events(&quit_requested);
    if (quit_requested) {
        uae_quit();
    }
    unix_host_check_quit();
    return debugger_active != 0;
}

static void openconsole(void)
{
    init_debugger_type();
    if (debugger_active && debugger_type == 2 && gui_debugger_available()) {
        consoleopen = 1;
        return;
    }
    if (terminal_console_available()) {
        consoleopen = -1;
    } else if (debugger_active && gui_debugger_available()) {
        consoleopen = 1;
    } else {
        consoleopen = 0;
    }
}

static void console_put(const TCHAR *text)
{
    if (!text) {
        return;
    }
    if (console_buffer) {
        const size_t used = _tcslen(console_buffer);
        const size_t len = _tcslen(text);
        if (used + len < size_t(console_buffer_size)) {
            _tcscat(console_buffer, text);
        }
        return;
    }

    if (!consoleopen) {
        openconsole();
    }
    if (consoleopen > 0) {
        unix_gui_debugger_write(text);
    } else {
        fputs(text, stderr);
        fflush(stderr);
    }
    if (debugfile) {
        fputs(text, debugfile);
        if (always_flush_log) {
            fflush(debugfile);
        }
    }
    capture_log_bytes(text, strlen(text));
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

TCHAR *setconsolemode(TCHAR *buffer, int maxlen)
{
    TCHAR *ret = NULL;
    if (buffer) {
        console_buffer = buffer;
        console_buffer_size = maxlen;
    } else {
        ret = console_buffer;
        console_buffer = NULL;
        console_buffer_size = 0;
    }
    return ret;
}

void close_console(void)
{
    if (consoleopen > 0) {
        unix_gui_debugger_close();
    }
    consoleopen = 0;
}

void open_console(void)
{
    if (!consoleopen) {
        openconsole();
    }
}

bool is_interactive_console(void)
{
    return terminal_console_available() || gui_debugger_available();
}

void reopen_console(void) {}
void activate_console(void)
{
    init_debugger_type();
    const bool wants_gui = debugger_active
        && debugger_type == 2
        && gui_debugger_available();
    if ((wants_gui && consoleopen < 0) || (!wants_gui && consoleopen > 0)) {
        close_console();
    }
    open_console();
}
void deactivate_console(void) {}
void set_console_input_mode(int) {}
bool is_console_open(void) { return consoleopen != 0; }
void console_out(const TCHAR *s)
{
    console_put(s);
}
void console_out_f(const TCHAR *format, ...)
{
    va_list size_args;
    va_start(size_args, format);
    const int needed = vsnprintf(NULL, 0, format, size_args);
    va_end(size_args);
    if (needed <= 0) {
        return;
    }

    std::vector<char> buffer(size_t(needed) + 1);
    va_list format_args;
    va_start(format_args, format);
    vsnprintf(buffer.data(), buffer.size(), format, format_args);
    va_end(format_args);
    console_put(buffer.data());
}
void console_flush(void)
{
    fflush(stderr);
    if (debugfile) {
        fflush(debugfile);
    }
}

int console_get(TCHAR *out, int maxlen)
{
    if (!out || maxlen <= 0) {
        return 0;
    }
    out[0] = 0;
    if (console_buffer) {
        return 0;
    }
    open_console();
    if (consoleopen > 0) {
        return unix_gui_debugger_get_input(out, maxlen);
    }
    if (!terminal_console_available()) {
        return -1;
    }
    for (;;) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(STDIN_FILENO, &set);
        timeval timeout = {};
        timeout.tv_usec = 100000;
        const int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
        if (ready > 0) {
            if (!fgets(out, maxlen, stdin)) {
                return -1;
            }
            break;
        }
        if (ready < 0 && errno != EINTR) {
            return -1;
        }
        if (!poll_host_window_events()) {
            return -1;
        }
    }
    size_t len = _tcslen(out);
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
        out[--len] = 0;
    }
    return int(len);
}

bool console_isch(void)
{
    if (console_buffer || !terminal_console_available()) {
        return false;
    }
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    timeval timeout = {};
    return select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout) > 0;
}

TCHAR console_getch(void)
{
    if (console_buffer || !terminal_console_available()) {
        return 0;
    }
    char ch = 0;
    while (read(STDIN_FILENO, &ch, 1) < 0) {
        if (errno != EINTR) {
            return 0;
        }
    }
    return ch;
}

void debugger_change(int mode)
{
    init_debugger_type();
    if (mode < 0) {
        debugger_type = debugger_type == 2 ? 1 : 2;
    } else {
        debugger_type = mode;
    }
    if (debugger_type == 2 && !gui_debugger_available()) {
        debugger_type = 1;
    }
    if (debugger_type == 1 && !terminal_console_available() && gui_debugger_available()) {
        debugger_type = 2;
    }
    if (debugger_type != 1 && debugger_type != 2) {
        debugger_type = gui_debugger_available() ? 2 : 1;
    }
    regsetint(NULL, _T("DebuggerType"), debugger_type);
    if (consoleopen > 0 && debugger_type != 2) {
        unix_gui_debugger_close();
    }
    consoleopen = 0;
    openconsole();
}

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
