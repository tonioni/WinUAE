#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "od-generic/mman_host.h"

#include <unistd.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

void host_mman_get_memory(uae_u64 *totalphys, uae_u64 *total, bool *os_64bit)
{
    uae_u64 phys = 0;
#if defined(__APPLE__)
    int mib[2] = { CTL_HW, HW_MEMSIZE };
    size_t len = sizeof phys;
    if (sysctl(mib, 2, &phys, &len, NULL, 0) != 0) {
        phys = 0;
    }
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page_size = sysconf(_SC_PAGESIZE);
    if (pages > 0 && page_size > 0) {
        phys = (uae_u64)pages * (uae_u64)page_size;
    }
#endif
    *totalphys = phys;
    *total = phys;
#ifdef CPU_64_BIT
    *os_64bit = true;
#else
    *os_64bit = false;
#endif
}

void mman_set_barriers(bool)
{
}
