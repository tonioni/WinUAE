#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "memory.h"
#include "uae/mman.h"
#include "uae/vm.h"
#include "autoconf.h"

bool jit_direct_compatible_memory;
size_t max_z3fastmem = 256 * 1024 * 1024;
size_t max_physmem = 512 * 1024 * 1024;

static struct uae_shmid_ds shmids[MAX_SHMID];
static size_t shm_allocsizes[MAX_SHMID];
static void *shm_heapallocs[MAX_SHMID];

static void clear_shmids(void)
{
    for (int i = 0; i < MAX_SHMID; i++) {
        memset(&shmids[i], 0, sizeof shmids[i]);
        shmids[i].key = -1;
        shm_allocsizes[i] = 0;
        shm_heapallocs[i] = NULL;
    }
}

static size_t page_round(size_t size)
{
    const size_t page_size = uae_vm_page_size();
    return (size + page_size - 1) & ~(page_size - 1);
}

static int find_shmkey(uae_key_t key)
{
    if (key >= 0 && key < MAX_SHMID && shmids[key].key == key) {
        return key;
    }
    return -1;
}

static int get_next_shmkey(void)
{
    for (int i = 0; i < MAX_SHMID; i++) {
        if (shmids[i].key == -1) {
            shmids[i].key = i;
            return i;
        }
    }
    return -1;
}

static int find_shmid_by_address(const void *address)
{
    for (int i = 0; i < MAX_SHMID; i++) {
        if (shmids[i].key != -1 && shmids[i].attached == address) {
            return i;
        }
    }
    return -1;
}

static size_t shmid_protect_size(int shmid)
{
    const size_t protectsize = page_round(shmids[shmid].rosize);
    if (shm_allocsizes[shmid] && protectsize > shm_allocsizes[shmid]) {
        return shm_allocsizes[shmid];
    }
    return protectsize;
}

static void *alloc_page_aligned(size_t size, void **rawmem)
{
    const size_t page_size = uae_vm_page_size();
    uae_u8 *raw = xcalloc(uae_u8, size);

    if (!raw) {
        return NULL;
    }
    if ((uintptr_t)raw & (page_size - 1)) {
        xfree(raw);
        return NULL;
    }

    *rawmem = raw;
    return raw;
}

static void release_shmid(int shmid)
{
    if (find_shmkey(shmid) == -1) {
        return;
    }
    if (shmids[shmid].attached) {
        if (shm_heapallocs[shmid]) {
            if (shmids[shmid].mode == UAE_VM_READ && shmids[shmid].rosize) {
                uae_vm_protect(shmids[shmid].attached, shmid_protect_size(shmid),
                    UAE_VM_READ_WRITE);
            }
            xfree(shm_heapallocs[shmid]);
        } else {
            uae_vm_free(shmids[shmid].attached, shm_allocsizes[shmid]);
        }
    }
    memset(&shmids[shmid], 0, sizeof shmids[shmid]);
    shmids[shmid].key = -1;
    shm_allocsizes[shmid] = 0;
    shm_heapallocs[shmid] = NULL;
}

static bool fill_rom_mman_info(addrbank *ab, struct uae_mman_data *md)
{
    if (!ab || !ab->label) {
        return false;
    }

    bool got = false;
    bool readonly = false;
    bool maprom = false;
    bool barrier = false;
    uaecptr start = ab->start;
    uae_u32 readonlysize = ab->reserved_size;

    if (!_tcscmp(ab->label, _T("kick"))) {
        start = 0xf80000;
        got = true;
        readonly = true;
        maprom = true;
        barrier = true;
    } else if (!_tcscmp(ab->label, _T("rom_a8"))) {
        start = 0xa80000;
        got = true;
        readonly = true;
        maprom = true;
    } else if (!_tcscmp(ab->label, _T("rom_e0"))) {
        start = 0xe00000;
        got = true;
        readonly = true;
        maprom = true;
    } else if (!_tcscmp(ab->label, _T("rom_f0"))) {
        start = 0xf00000;
        got = true;
        readonly = true;
    } else if (!_tcscmp(ab->label, _T("rtarea"))) {
        start = rtarea_base;
        got = true;
        readonly = true;
        readonlysize = RTAREA_TRAPS;
    }

    if (!got) {
        return false;
    }

    memset(md, 0, sizeof *md);
    md->start = start;
    md->size = ab->reserved_size + (barrier ? 32 : 0);
    md->readonly = readonly;
    md->readonlysize = readonlysize;
    md->maprom = maprom;
    md->directsupport = false;
    md->hasbarrier = barrier;
    return true;
}

bool preinit_shm(void)
{
    clear_shmids();
    return true;
}

bool init_shm(void)
{
    jit_direct_compatible_memory = false;
    canbang = false;
    natmem_reserved = NULL;
    natmem_offset = NULL;
    natmem_reserved_size = 0;
    clear_shmids();
    return true;
}

void free_shm(void)
{
    for (int i = 0; i < MAX_SHMID; i++) {
        release_shmid(i);
    }
}

bool uae_mman_info(addrbank *ab, struct uae_mman_data *md)
{
    return fill_rom_mman_info(ab, md);
}

void mapped_free(addrbank *ab)
{
    if (!ab) {
        return;
    }
    const int shmid = find_shmid_by_address(ab->baseaddr);
    if (shmid >= 0) {
        release_shmid(shmid);
    } else if (!(ab->flags & ABFLAG_NOALLOC)) {
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

bool uae_mman_alloc_nodirect(addrbank *ab, uae_u32 size)
{
    struct uae_mman_data md;

    if (!fill_rom_mman_info(ab, &md)) {
        return false;
    }

    const int shmid = get_next_shmkey();
    if (shmid < 0) {
        return false;
    }

    const size_t requested = md.size > size ? md.size : size;
    const size_t allocsize = page_round(requested);
    void *rawmem = NULL;
    void *result = alloc_page_aligned(allocsize, &rawmem);
    if (!result) {
        write_log(_T("MMAN: failed to allocate %zu bytes for %s (%s)\n"),
            allocsize, ab && ab->label ? ab->label : _T("?"),
            ab && ab->name ? ab->name : _T("?"));
        release_shmid(shmid);
        return false;
    }

    shmids[shmid].size = ab->reserved_size;
    _tcscpy(shmids[shmid].name, ab->label ? ab->label : _T(""));
    shmids[shmid].attached = result;
    shmids[shmid].mode = md.readonly ? UAE_VM_READ : UAE_VM_READ_WRITE;
    shmids[shmid].rosize = md.readonlysize;
    shmids[shmid].maprom = md.maprom ? 1 : 0;
    shmids[shmid].natmembase = NULL;
    shm_allocsizes[shmid] = allocsize;
    shm_heapallocs[shmid] = rawmem;
    ab->baseaddr = (uae_u8 *)result;
    write_log(_T("MMAN: allocated %s %p-%p %zu (%zuk)%s\n"),
        ab && ab->label ? ab->label : _T("?"),
        result, (uae_u8 *)result + allocsize, allocsize, allocsize >> 10,
        md.readonly ? _T(" readonly-capable") : _T(""));
    return true;
}

void *uae_shmat(addrbank *ab, int shmid, void *, int, struct uae_mman_data *md)
{
    struct uae_mman_data local_md;

    if (find_shmkey(shmid) == -1) {
        return (void *)-1;
    }
    if (shmids[shmid].attached) {
        return shmids[shmid].attached;
    }
    if (!md) {
        if (!fill_rom_mman_info(ab, &local_md)) {
            return (void *)-1;
        }
        md = &local_md;
    }

    const size_t allocsize = page_round(md->size);
    void *result = uae_vm_alloc(allocsize, 0, UAE_VM_READ_WRITE);
    if (!result) {
        write_log(_T("MMAN: failed to allocate %zu bytes for %s (%s)\n"),
            allocsize, ab && ab->label ? ab->label : _T("?"),
            ab && ab->name ? ab->name : _T("?"));
        return (void *)-1;
    }

    shmids[shmid].attached = result;
    shmids[shmid].mode = md->readonly ? UAE_VM_READ : UAE_VM_READ_WRITE;
    shmids[shmid].rosize = md->readonlysize;
    shmids[shmid].maprom = md->maprom ? 1 : 0;
    shmids[shmid].natmembase = NULL;
    shm_allocsizes[shmid] = allocsize;
    shm_heapallocs[shmid] = NULL;
    write_log(_T("MMAN: allocated %s %p-%p %zu (%zuk)%s\n"),
        ab && ab->label ? ab->label : _T("?"),
        result, (uae_u8 *)result + allocsize, allocsize, allocsize >> 10,
        md->readonly ? _T(" readonly-capable") : _T(""));
    return result;
}

int uae_shmdt(const void *address)
{
    const int shmid = find_shmid_by_address(address);
    if (shmid >= 0) {
        return 0;
    }
    return 0;
}

int uae_shmget(uae_key_t key, addrbank *ab, int shmflg)
{
    int result = -1;

    if (key == UAE_IPC_PRIVATE ||
        ((shmflg & UAE_IPC_CREAT) && find_shmkey(key) == -1)) {
        result = get_next_shmkey();
        if (result != -1) {
            shmids[result].size = ab->reserved_size;
            _tcscpy(shmids[result].name, ab->label ? ab->label : _T(""));
            write_log(_T("shmget of size %d (%dk) for %s (%s)\n"),
                ab->reserved_size, ab->reserved_size >> 10,
                ab->label ? ab->label : _T("?"),
                ab->name ? ab->name : _T("?"));
        }
    }
    return result;
}

int uae_shmctl(int shmid, int cmd, struct uae_shmid_ds *buf)
{
    if (find_shmkey(shmid) == -1 || !buf) {
        return -1;
    }

    switch (cmd) {
    case UAE_IPC_STAT:
        *buf = shmids[shmid];
        return 0;
    case UAE_IPC_RMID:
        release_shmid(shmid);
        return 0;
    default:
        return -1;
    }
}

void unprotect_maprom(void)
{
    for (int i = 0; i < MAX_SHMID; i++) {
        struct uae_shmid_ds *shm = &shmids[i];
        if (shm->mode != UAE_VM_READ) {
            continue;
        }
        if (!shm->attached || !shm->rosize || shm->maprom <= 0) {
            continue;
        }
        shm->maprom = -1;
        const size_t protectsize = shmid_protect_size(i);
        if (!uae_vm_protect(shm->attached, protectsize, UAE_VM_READ_WRITE)) {
            write_log(_T("unprotect_maprom mprotect %p - %p %x (%dk) failed\n"),
                shm->attached, (uae_u8 *)shm->attached + protectsize,
                (uae_u32)protectsize, (uae_u32)protectsize >> 10);
        }
    }
}

void protect_roms(bool protect)
{
    if (protect) {
        // Match Windows: protect ROMs only for strict JIT modes.
        if (!currprefs.cachesize || currprefs.comptrustbyte ||
            currprefs.comptrustword || currprefs.comptrustlong) {
            return;
        }
    }

    for (int i = 0; i < MAX_SHMID; i++) {
        struct uae_shmid_ds *shm = &shmids[i];
        if (shm->mode != UAE_VM_READ) {
            continue;
        }
        if (!shm->attached || !shm->rosize) {
            continue;
        }
        if (shm->maprom < 0 && protect) {
            continue;
        }

        const int mode = protect ? UAE_VM_READ : UAE_VM_READ_WRITE;
        const size_t protectsize = shmid_protect_size(i);
        if (!uae_vm_protect(shm->attached, protectsize, mode)) {
            write_log(_T("protect_roms mprotect %p - %p %x (%dk) failed\n"),
                shm->attached, (uae_u8 *)shm->attached + protectsize,
                (uae_u32)protectsize, (uae_u32)protectsize >> 10);
        } else {
            write_log(_T("ROM mprotect %p - %p %x (%dk) %s\n"),
                shm->attached, (uae_u8 *)shm->attached + protectsize,
                (uae_u32)protectsize, (uae_u32)protectsize >> 10,
                protect ? _T("WPROT") : _T("UNPROT"));
        }
    }
}
