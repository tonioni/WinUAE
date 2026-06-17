#include "sysconfig.h"
#include "sysdeps.h"

#include "gfxboard.h"
#include "pcem/device.h"
#include "pcem/io.h"
#include "pcem/timer.h"
#include "threaddep/thread.h"

#include <unistd.h>

typedef struct mem_mapping_t mem_mapping_t;

void mem_mapping_addx(mem_mapping_t *mapping, uint32_t base, uint32_t size,
    uint8_t (*read_b)(uint32_t addr, void *p),
    uint16_t (*read_w)(uint32_t addr, void *p),
    uint32_t (*read_l)(uint32_t addr, void *p),
    void (*write_b)(uint32_t addr, uint8_t val, void *p),
    void (*write_w)(uint32_t addr, uint16_t val, void *p),
    void (*write_l)(uint32_t addr, uint32_t val, void *p),
    uint8_t *exec, uint32_t flags, void *p);
void mem_mapping_set_handlerx(mem_mapping_t *mapping,
    uint8_t (*read_b)(uint32_t addr, void *p),
    uint16_t (*read_w)(uint32_t addr, void *p),
    uint32_t (*read_l)(uint32_t addr, void *p),
    void (*write_b)(uint32_t addr, uint8_t val, void *p),
    void (*write_w)(uint32_t addr, uint16_t val, void *p),
    void (*write_l)(uint32_t addr, uint32_t val, void *p));
void mem_mapping_set_px(mem_mapping_t *mapping, void *p);
void mem_mapping_set_addrx(mem_mapping_t *mapping, uint32_t base, uint32_t size);
void mem_mapping_disablex(mem_mapping_t *mapping);
void mem_mapping_enablex(mem_mapping_t *mapping);

#ifndef WITH_X86
uint64_t tsc;
uint64_t VGACONST1;
uint64_t VGACONST2;
float cpuclock;
int has_vlb;

static struct pcem_timer_defaults
{
    pcem_timer_defaults()
    {
        if (!cpuclock) {
            cpuclock = 33000000.0f;
        }
        if (!VGACONST1) {
            VGACONST1 = (uint64_t)((cpuclock / 25175000.0) * (float)(1ULL << 32));
        }
        if (!VGACONST2) {
            VGACONST2 = (uint64_t)((cpuclock / 28322000.0) * (float)(1ULL << 32));
        }
        if (!TIMER_USEC) {
            TIMER_USEC = 1ULL << 32;
        }
    }
} pcem_timer_defaults_instance;
#endif

HANDLE CreateSemaphore(void*, int, int initial_count, const char*)
{
    uae_sem_t sem = NULL;
    uae_sem_init(&sem, 0, initial_count > 0 ? 1 : 0);
    return (HANDLE)sem;
}

DWORD WaitForSingleObject(HANDLE handle, DWORD timeout)
{
    uae_sem_t sem = (uae_sem_t)handle;
    int ms = timeout == INFINITE ? -1 : (int)timeout;
    return uae_sem_trywait_delay(&sem, ms) == 0 ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

BOOL ReleaseSemaphore(HANDLE handle, int, void*)
{
    uae_sem_t sem = (uae_sem_t)handle;
    uae_sem_post(&sem);
    return TRUE;
}

BOOL CloseHandle(HANDLE handle)
{
    uae_sem_t sem = (uae_sem_t)handle;
    uae_sem_destroy(&sem);
    return TRUE;
}

#ifndef WITH_X86
void mem_mapping_add(mem_mapping_t *mapping,
    uint32_t base,
    uint32_t size,
    uint8_t (*read_b)(uint32_t addr, void *p),
    uint16_t (*read_w)(uint32_t addr, void *p),
    uint32_t (*read_l)(uint32_t addr, void *p),
    void (*write_b)(uint32_t addr, uint8_t val, void *p),
    void (*write_w)(uint32_t addr, uint16_t val, void *p),
    void (*write_l)(uint32_t addr, uint32_t val, void *p),
    uint8_t *exec,
    uint32_t flags,
    void *p)
{
    mem_mapping_addx(mapping, base, size, read_b, read_w, read_l, write_b,
        write_w, write_l, exec, flags, p);
}

void mem_mapping_set_handler(mem_mapping_t *mapping,
    uint8_t (*read_b)(uint32_t addr, void *p),
    uint16_t (*read_w)(uint32_t addr, void *p),
    uint32_t (*read_l)(uint32_t addr, void *p),
    void (*write_b)(uint32_t addr, uint8_t val, void *p),
    void (*write_w)(uint32_t addr, uint16_t val, void *p),
    void (*write_l)(uint32_t addr, uint32_t val, void *p))
{
    mem_mapping_set_handlerx(mapping, read_b, read_w, read_l, write_b,
        write_w, write_l);
}

void mem_mapping_set_p(mem_mapping_t *mapping, void *p)
{
    mem_mapping_set_px(mapping, p);
}

void mem_mapping_set_addr(mem_mapping_t *mapping, uint32_t base, uint32_t size)
{
    mem_mapping_set_addrx(mapping, base, size);
}

void mem_mapping_set_exec(mem_mapping_t*, uint8_t*)
{
}

void mem_mapping_disable(mem_mapping_t *mapping)
{
    mem_mapping_disablex(mapping);
}

void mem_mapping_enable(mem_mapping_t *mapping)
{
    mem_mapping_enablex(mapping);
}

void io_sethandler(uint16_t base, int size,
    uint8_t (*inb)(uint16_t addr, void *priv),
    uint16_t (*inw)(uint16_t addr, void *priv),
    uint32_t (*inl)(uint16_t addr, void *priv),
    void (*outb)(uint16_t addr, uint8_t val, void *priv),
    void (*outw)(uint16_t addr, uint16_t val, void *priv),
    void (*outl)(uint16_t addr, uint32_t val, void *priv),
    void *priv)
{
    io_sethandlerx(base, size, inb, inw, inl, outb, outw, outl, priv);
}

void io_removehandler(uint16_t base, int size,
    uint8_t (*inb)(uint16_t addr, void *priv),
    uint16_t (*inw)(uint16_t addr, void *priv),
    uint32_t (*inl)(uint16_t addr, void *priv),
    void (*outb)(uint16_t addr, uint8_t val, void *priv),
    void (*outw)(uint16_t addr, uint16_t val, void *priv),
    void (*outl)(uint16_t addr, uint32_t val, void *priv),
    void *priv)
{
    io_removehandlerx(base, size, inb, inw, inl, outb, outw, outl, priv);
}

extern void put_io_pcem(uaecptr addr, uae_u32 v, int size);
extern uae_u32 get_io_pcem(uaecptr addr, int size);

uint8_t portin(uint16_t portnum)
{
    return (uint8_t)get_io_pcem(portnum, 0);
}

void portout(uint16_t portnum, uint8_t value)
{
    put_io_pcem(portnum, value, 0);
}

uint16_t portin16(uint16_t portnum)
{
    return (uint16_t)get_io_pcem(portnum, 1);
}

void portout16(uint16_t portnum, uint16_t value)
{
    put_io_pcem(portnum, value, 1);
}

uint32_t portin32(uint16_t portnum)
{
    return get_io_pcem(portnum, 2);
}

void portout32(uint16_t portnum, uint32_t value)
{
    put_io_pcem(portnum, value, 2);
}

void x86_map_lfb(int)
{
}

static int pcem_unix_render_threads(void)
{
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpus >= 8) {
        return 4;
    }
    if (cpus >= 4) {
        return 2;
    }
    return 1;
}

int device_get_config_int(const char *name)
{
    if (!strcmp(name, "bilinear") || !strcmp(name, "dithering") ||
        !strcmp(name, "dithersub") || !strcmp(name, "dacfilter")) {
        return 1;
    }
    if (!strcmp(name, "recompiler")) {
#ifdef PCEM_VOODOO_CODEGEN
        return 1;
#else
        return 0;
#endif
    }
    if (!strcmp(name, "sli") || !strcmp(name, "type")) {
        return 0;
    }
    if (!strcmp(name, "framebuffer_memory") ||
        !strcmp(name, "texture_memory")) {
        return 2;
    }
    if (!strcmp(name, "memory")) {
        int vram = pcem_getvramsize() >> 20;
        return vram > 0 ? vram : 2;
    }
    if (!strcmp(name, "render_threads")) {
        return pcem_unix_render_threads();
    }
    return 0;
}
#endif

char *device_get_config_string(const char*)
{
    return NULL;
}

char *current_device_name;
