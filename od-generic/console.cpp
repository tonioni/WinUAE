#include "sysconfig.h"
#include "sysdeps.h"
#include "console.h"

#include <vector>
#include <stdarg.h>

static TCHAR *console_buffer;
static size_t console_capacity;

bool console_is_buffered(void)
{
	return console_buffer != NULL;
}

TCHAR *setconsolemode(TCHAR *buffer, int maxlen)
{
	TCHAR *previous = console_buffer;
	console_buffer = buffer;
	console_capacity = buffer && maxlen > 0 ? (size_t)maxlen : 0;
	return buffer ? NULL : previous;
}

void console_out(const TCHAR *text)
{
	if (!text)
		return;
	if (console_buffer && console_capacity) {
		size_t used = 0;
		while (used < console_capacity && console_buffer[used])
			used++;
		size_t len = _tcslen(text);
		// Keep the existing all-or-nothing capture contract, including NUL.
		if (used < console_capacity && len < console_capacity - used)
			memcpy(console_buffer + used, text, (len + 1) * sizeof(TCHAR));
	}
	target_console_out(text, console_is_buffered());
}

void console_out_f(const TCHAR *format, ...)
{
	std::vector<TCHAR> buffer(1024);
	va_list args;
	va_start(args, format);
	for (;;) {
		va_list copy;
		va_copy(copy, args);
		int len = _vsntprintf(buffer.data(), buffer.size(), format, copy);
		va_end(copy);
		if (len >= 0 && (size_t)len < buffer.size()) {
			if (len)
				console_out(buffer.data());
			break;
		}
		// MSVC reports -1 for truncation; C99 returns the required size.
		size_t next = len < 0 ? buffer.size() * 2 : (size_t)len + 1;
		if (next <= buffer.size() || next > 10000000)
			break;
		buffer.resize(next);
	}
	va_end(args);
}
