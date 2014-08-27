/*
 * Defines useful functions and variable attributes for UAE
 * Copyright (C) 2014 Frode Solheim
 *
 * Licensed under the terms of the GNU General Public License version 2.
 * See the file 'COPYING' for full license text.
 */

#ifndef UAE_ATTRIBUTES_H
#define UAE_ATTRIBUTES_H

/* This file is intended to be included by external libraries as well,
 * so don't pull in too much UAE-specific stuff. */

/* This attribute allows (some) compiles to emit warnings when incorrect
 * arguments are used with the format string. */

#ifdef __GNUC__
#define UAE_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define UAE_PRINTF_FORMAT(f, a)
#endif

#endif /* UAE_ATTRIBUTES_H */
