#include "uae/logging.h"
#include <stdio.h>
#include <stdarg.h>

//UAEAPI void UAECALL uae_log(const char *format, ...)
void UAECALL uae_log(const char *format, ...)
{
	char buffer[1000];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buffer, 1000, format, ap);
	write_log("%s", buffer);
	va_end(ap);
}
