/*
 * Helpers used to export UAE functions for use with other modules
 * Copyright (C) 2014 Frode Solheim
 *
 * Licensed under the terms of the GNU General Public License version 2.
 * See the file 'COPYING' for full license text.
 */

#ifndef UAE_API_H
#define UAE_API_H

/* This file is intended to be included by external libraries as well,
 * so don't pull in too much UAE-specific stuff. */

#include "uae/attributes.h"

/* Handy define so we can disable C++ name mangling without considering
 * whether the source language is C or C++. */

#ifdef __cplusplus
#define UAE_EXTERN_C extern "C"
#else
#define UAE_EXTERN_C
#endif

/* UAE_EXPORT / UAE_IMPORT are mainly intended as helpers for UAEAPI
 * defined below. */

#ifdef _WIN32
#define UAE_EXPORT __declspec(dllexport)
#define UAE_IMPORT __declspec(dllimport)
#else
#define UAE_EXPORT __attribute__((visibility("default")))
#define UAE_IMPORT
#endif

/* UAEAPI marks a function for export across library boundaries. You'll
 * likely want to use this together with UAECALL. */

#ifdef UAE
#define UAEAPI UAE_EXTERN_C UAE_EXPORT
#else
#define UAEAPI UAE_EXTERN_C UAE_IMPORT
#endif

/* WinUAE (or external libs) might be compiled with fastcall by default,
 * so we force all external functions to use cdecl calling convention. */

#ifdef _WIN32
#define UAECALL __cdecl
#else
#define UAECALL
#endif

#endif /* UAE_API_H */
