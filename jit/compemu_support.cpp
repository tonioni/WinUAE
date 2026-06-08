#if defined(CPU_arm) || defined(CPU_AARCH64) || defined(__aarch64__) || \
    defined(_M_ARM64) || defined(_M_ARM64EC)
#include "arm/compemu_support_arm.cpp"
#elif defined(CPU_i386) || defined(CPU_x86_64) || defined(__i386__) || \
    defined(__x86_64__) || defined(_M_IX86) || defined(_M_AMD64)
#include "x86/compemu_support_x86.cpp"
#endif
