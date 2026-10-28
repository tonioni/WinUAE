/*
 * UAE calling-convention and printf-format attribute macros.
 *
 * Must stay self-contained: no includes, so it can be pulled in by api.h
 * and directly by sources that only need uae_cdecl.
 *
 * Copyright (C) 2026 Dimitris Panokostas
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Clean-room implementation written from a functional description of the
 * UAE portability interface.
 */

#ifndef UAE_ATTRIBUTES_H
#define UAE_ATTRIBUTES_H

/* _WIN32 is tested first: the Win32 MSVC project defines __i386__ itself,
 * and MSVC does not accept the GNU attribute syntax. */
#if defined(_WIN32)
/* Windows __cdecl keyword (MSVC, and clang/gcc targeting Windows) */
#define uae_cdecl __cdecl
#elif defined(__GNUC__) && defined(__i386__)
/* 32-bit x86 GCC/Clang: GNU cdecl attribute */
#define uae_cdecl __attribute__((cdecl))
#else
#define uae_cdecl
#endif

#ifdef __GNUC__
/* Archetype must be plain "printf" (not gnu_printf/ms_printf) so that
 * format checking also accepts Amiga-specific %ld-style usage unchanged. */
#define UAE_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define UAE_PRINTF_FORMAT(f, a)
#endif

/* Wide printf formats are used (the TCHAR write_log overload in UNICODE
 * builds), but no supported compiler can check them, so this is empty. */
#define UAE_WPRINTF_FORMAT(f, a)

#endif /* UAE_ATTRIBUTES_H */
