#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "uae/mman.h"

bool jit_direct_compatible_memory;
size_t max_z3fastmem = 256 * 1024 * 1024;
size_t max_physmem = 512 * 1024 * 1024;

bool preinit_shm(void)
{
    return true;
}

bool init_shm(void)
{
    jit_direct_compatible_memory = false;
    canbang = false;
    return true;
}

void free_shm(void)
{
}

bool uae_mman_info(addrbank *, struct uae_mman_data *)
{
    return false;
}

void mapped_free(addrbank *ab)
{
    if (!ab) {
        return;
    }
    if (!(ab->flags & ABFLAG_NOALLOC)) {
        xfree(ab->baseaddr);
    }
    ab->flags &= ~(ABFLAG_MAPPED | ABFLAG_DIRECTMAP);
    ab->allocated_size = 0;
    ab->baseaddr = NULL;
    ab->baseaddr_direct_r = NULL;
    ab->baseaddr_direct_w = NULL;
}

void mman_set_barriers(bool)
{
}

void commit_natmem_gaps(void)
{
}

void *uae_shmat(addrbank *, int, void *, int, struct uae_mman_data *)
{
    return (void *)-1;
}

int uae_shmdt(const void *)
{
    return 0;
}

int uae_shmget(uae_key_t, addrbank *, int)
{
    return -1;
}

int uae_shmctl(int, int, struct uae_shmid_ds *)
{
    return 0;
}
