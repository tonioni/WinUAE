
#include <cstring>
#include <cstdio>
#include <stdarg.h>

#include "system/systhread.h"

void write_log(const char *format, ...);

int ht_printf(const char *fmt,...)
{
	char buffer[1000];
	va_list parms;
	va_start(parms, fmt);
	vsprintf(buffer, fmt, parms);
	write_log(buffer);
	va_end(parms);
	return 0;
}
int ht_fprintf(FILE *f, const char *fmt, ...)
{
	return 0;
}
int ht_vfprintf(FILE *file, const char *fmt, va_list args)
{
	return 0;
}
int ht_snprintf(char *str, size_t count, const char *fmt, ...)
{
	return 0;
}
int ht_vsnprintf(char *str, size_t count, const char *fmt, va_list args)
{
	return 0;
}

void ht_assert_failed(const char *file, int line, const char *assertion)
{
}

void prom_quiesce()
{
}

