#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "uae/vm.h"
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

// Mark indirect regions (indirect VRAM) as non-accessible when JIT direct is
// active, like Windows does. mprotect() doesn't return the old protection, so
// restore what the region had: read-only gap pages on ARM64 (see
// commit_natmem_gaps()), otherwise read-write so that the barrier at the end
// of the previous bank stays readable.
void mman_set_barriers(bool disable)
{
    if (!natmem_reserved || !natmem_offset) {
        return;
    }
#if defined(CPU_AARCH64)
    const int restore = canbang ? UAE_VM_READ : UAE_VM_READ_WRITE;
#else
    const int restore = UAE_VM_READ_WRITE;
#endif
    const bool noaccess = !disable && currprefs.cachesize && !currprefs.comptrustbyte &&
        !currprefs.comptrustword && !currprefs.comptrustlong;
    addrbank *abprev = NULL;
    int maxbank = currprefs.address_space_24 ? MEMORY_BANKS_24 : MEMORY_BANKS;
    for (int i = 0; i < maxbank; i++) {
        uaecptr addr = i * 0x10000;
        addrbank *ab = &get_mem_bank(addr);
        if (ab == abprev) {
            continue;
        }
        int size = 0x10000;
        for (int j = i + 1; j < maxbank; j++) {
            uaecptr addr2 = j * 0x10000;
            addrbank *ab2 = &get_mem_bank(addr2);
            if (ab2 != ab) {
                break;
            }
            size += 0x10000;
        }
        abprev = ab;
        if (!ab || ab->baseaddr != NULL || !(ab->flags & ABFLAG_ALLOCINDIRECT)) {
            continue;
        }
        uae_u8 *p = natmem_offset + addr;
        if (p < natmem_reserved || p + size > natmem_reserved + natmem_reserved_size) {
            continue;
        }
        if (noaccess) {
            if (uae_vm_protect(p, size, UAE_VM_NO_ACCESS)) {
                ab->protectmode = 1;
                write_log(_T("%08x-%08x = set to no access\n"), addr, addr + size);
            }
        } else if (ab->protectmode) {
            uae_vm_protect(p, size, restore);
            ab->protectmode = 0;
            write_log(_T("%08x-%08x = access restored\n"), addr, addr + size);
        }
    }
}
