#ifndef WINUAE_OD_UNIX_MACHDEP_H
#define WINUAE_OD_UNIX_MACHDEP_H

#if defined(__x86_64__) || defined(__i386__)
#define MACHDEP_X86 1
#define MACHDEP_NAME "x86"
#elif defined(__aarch64__) || defined(__arm__)
#define MACHDEP_ARM 1
#define MACHDEP_NAME "arm"
#else
#define MACHDEP_NAME "generic"
#endif

#define HAVE_MACHDEP_TIMER 1

#endif /* WINUAE_OD_UNIX_MACHDEP_H */
