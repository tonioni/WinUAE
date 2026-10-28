/*
 * UAE logging interface.
 *
 * In a UAE build this declares uae_log() (the plugin-facing log entry point)
 * and write_log() itself. In a plugin build uae_log is an extern function
 * pointer that the host fills in after loading the plugin
 * (uae_dlopen_patch_common).
 *
 * The macros below are brace-enclosed statement blocks; <cstdarg> and
 * <cstdio> must be visible at the expansion site (sysdeps.h provides them).
 *
 * Copyright (C) 2026 Dimitris Panokostas
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Clean-room implementation written from a functional description of the
 * UAE portability interface.
 */

#ifndef UAE_LOG_H
#define UAE_LOG_H

#include "uae/api.h"
#include "uae/attributes.h"
#include "uae/types.h"

/* Function-pointer type of the plugin-facing log hook. In plugin builds
 * UAE exports `uae_log` as a data symbol of exactly this type. */
typedef void (UAECALL *uae_log_function)(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);

#ifdef UAE

/* WinUAE defines this in logging.cpp (forwarding to write_log through
 * UAE_LOG_VA_ARGS_FULL) and hands its address to plugins; hosts without a
 * definition patch the plugin's hook with write_log instead. It must keep
 * UAECALL so that its address converts to uae_log_function. */
void UAECALL uae_log(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);

void write_log(const char *format, ...) UAE_PRINTF_FORMAT(1, 2);
#if SIZEOF_TCHAR != 1
void write_log(const TCHAR *format, ...) UAE_WPRINTF_FORMAT(1, 2);
#endif

#else /* plugin build */

extern uae_log_function uae_log;

#endif /* UAE */

/* Format `format` with va_list `ap` into a 1024-byte buffer (truncating)
 * and forward it to uae_log. The final call must stay spelled
 * uae_log("%s", ...): logging.cpp temporarily defines uae_log as
 * write_log around UAE_LOG_VA_ARGS_FULL to implement uae_log itself. */
#define UAE_LOG_VA_ARGS(format, ap) \
	{ \
		char uae_log_buffer[1024]; \
		vsnprintf(uae_log_buffer, sizeof(uae_log_buffer), format, ap); \
		uae_log_buffer[sizeof(uae_log_buffer) - 1] = '\0'; \
		uae_log("%s", uae_log_buffer); \
	}

/* Same, for use inside a variadic function whose last named parameter
 * is `format`. */
#define UAE_LOG_VA_ARGS_FULL(format) \
	{ \
		va_list uae_log_ap; \
		va_start(uae_log_ap, format); \
		UAE_LOG_VA_ARGS(format, uae_log_ap); \
		va_end(uae_log_ap); \
	}

/* Log a stub notice: " -- stub -- <function> <format>\n". Works with zero
 * variadic arguments. */
#define UAE_LOG_STUB(format, ...) \
	{ \
		uae_log(" -- stub -- %s " format "\n", __func__, ##__VA_ARGS__); \
	}

#endif /* UAE_LOG_H */
