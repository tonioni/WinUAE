/*
 * Logging functions for use with UAE and external modules
 * Copyright (C) 2014 Frode Solheim
 *
 * Licensed under the terms of the GNU General Public License version 2.
 * See the file 'COPYING' for full license text.
 */

#ifndef UAE_LOGGING_H
#define UAE_LOGGING_H

/* This file is intended to be included by external libraries as well,
 * so don't pull in too much UAE-specific stuff. */

#include "uae/api.h"
#include "uae/attributes.h"
#include "uae/types.h"

typedef void (UAECALL *uae_log_function)(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
#ifdef UAE
//UAEAPI void UAECALL uae_log(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
void UAECALL uae_log(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
#else
extern uae_log_function uae_log;
#endif

#if 0
void uae_warning(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
void uae_error(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
void uae_fatal(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
#endif

/* Helpers to log use of stubbed functions */

#define UAE_LOG_STUB(format, ...) \
{ \
	uae_log(" -- stub -- %s " format "\n", __func__, ##__VA_ARGS__); \
}

#define UAE_LOG_STUB_MAX(max, format, ...) \
{ \
	static int log_stub_count = 0; \
	if (log_stub_count < max) { \
		LOG_STUB(format, ##__VA_ARGS__) \
		if (++log_stub_count == max) { \
			uae_log("    (ignoring further calls to %s)\n", __func__); \
		} \
	} \
}

#define UAE_STUB(format, ...) \
{ \
	UAE_LOG_STUB(format, ##__VA_ARGS__) \
	printf(" -- stub -- %s " format "\n", __func__, ##__VA_ARGS__); \
}

/* UAE-specific functions */

#ifdef UAE

void write_log (const char *, ...) UAE_PRINTF_FORMAT(1, 2);
#if SIZEOF_TCHAR != 1
void write_log (const TCHAR *, ...) UAE_PRINTF_FORMAT(1, 2);
#endif

#endif

/* Deprecated defines */

#ifdef UAE

#define STUB UAE_STUB
#define LOG_STUB UAE_LOG_STUB
#define LOG_STUB_MAX UAE_LOG_STUB_MAX
#define VERBOSE_STUB STUB

#endif

#endif /* UAE_LOGGING_H */
