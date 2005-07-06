// Implement mprotect() for Win32
// Copyright (C) 2000, Brian King
// GNU Public License

#include <float.h>

#include "sysconfig.h" 
#include "sysdeps.h"
#include "sys/mman.h"
#include "include/memory.h"
#include "options.h"
#include "autoconf.h"
#include "win32.h"

#if defined(NATMEM_OFFSET)

static struct shmid_ds shmids[MAX_SHMID];
static uae_u32 gfxoffs;

uae_u8 *natmem_offset = NULL;
#ifdef CPU_64_BIT
uae_u32 max_allowed_mman = 2048;
#else
uae_u32 max_allowed_mman = 512;
#endif

static uae_u8 stackmagic64asm[] = {
    0x48,0x8b,0x44,0x24,0x28,	// mov rax,qword ptr [rsp+28h] 
    0x49,0x8b,0xe1,		// mov rsp,r9 
    0xff,0xd0			// call rax
};
uae_u8 *stack_magic_amd64_asm_executable;

void cache_free(void *cache)
{
    VirtualFree (cache, 0, MEM_RELEASE);
}

void *cache_alloc(int size)
{
    uae_u8 *cache;
    cache = VirtualAlloc (NULL, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    return cache;
}

void init_shm(void)
{
    static int allocated;
    int i;
    LPVOID blah = NULL;
    // Letting the system decide doesn't seem to work on some systems (Win9x..)
    LPBYTE address = (LPBYTE)0x10000000;
    uae_u32 size;
    uae_u32 add = 0x11000000;
    uae_u32 inc = 0x100000;
    MEMORYSTATUS memstats;

#ifdef CPU_64_BIT
    if (!stack_magic_amd64_asm_executable) {
	stack_magic_amd64_asm_executable = cache_alloc(sizeof stackmagic64asm);
	memcpy(stack_magic_amd64_asm_executable, stackmagic64asm, sizeof stackmagic64asm);
    }
#endif

    if (natmem_offset && os_winnt)
	VirtualFree(natmem_offset, 0, MEM_RELEASE);
    natmem_offset = NULL;

    memstats.dwLength = sizeof(memstats);
    GlobalMemoryStatus(&memstats);
    max_z3fastmem = 16 * 1024 * 1024;

    while ((uae_u64)memstats.dwAvailPageFile + (uae_u64)memstats.dwAvailPhys >= ((uae_u64)max_z3fastmem << 1)
	&& max_z3fastmem != ((uae_u64)2048 * 1024 * 1024))
	    max_z3fastmem <<= 1;
    size = max_z3fastmem;
    if (size > max_allowed_mman * 1024 * 1024)
	size = max_allowed_mman * 1024 * 1024;

    canbang = 0;
    gfxoffs = 0;
    shm_start = 0;
    for (i = 0; i < MAX_SHMID; i++) {
	shmids[i].attached = 0;
	shmids[i].key = -1;
	shmids[i].size = 0;
	shmids[i].addr = NULL;
	shmids[i].name[0] = 0;
    }
    for (;;) {
	blah = VirtualAlloc(NULL, size + add, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	if (blah)
	    break;
	size >>= 1;
	if (size < 0x10000000) {
	    write_log("NATMEM: No special area could be allocated (2)!\n");
	    return;
	}
    }
    if (os_winnt) {
	natmem_offset = blah;
    } else {
	VirtualFree(blah, 0, MEM_RELEASE);
        while (address < (LPBYTE)0xa0000000) {
	    blah = VirtualAlloc(address, size + add, MEM_RESERVE, PAGE_EXECUTE_READWRITE);
	    if (blah == NULL) {
		address += inc;
	    } else {
		VirtualFree (blah, 0, MEM_RELEASE);
		address += inc * 32;
		natmem_offset = address;
		break;
	    }
	}
    }

    if (!natmem_offset) {
	write_log("NATMEM: No special area could be allocated! (1)\n");
    } else {
	max_z3fastmem = size;
	write_log("NATMEM: Our special area: 0x%p-0x%p (%dM)\n",
	    natmem_offset, (uae_u8*)natmem_offset + size + add, (size + add) >> 20);
	canbang = 1;
	allocated = 1;
    }

    while (memstats.dwAvailPageFile + memstats.dwAvailPhys < max_z3fastmem)
        max_z3fastmem <<= 1;

    write_log("Max Z3FastRAM %dM\n", max_z3fastmem >> 20);
}


void mapped_free(uae_u8 *mem)
{
    shmpiece *x = shm_start;
    while( x )
    {
	if( mem == x->native_address )
	    shmdt( x->native_address);
	x = x->next;
    }
    x = shm_start;
    while( x )
    {
	struct shmid_ds blah;
	if( mem == x->native_address )
	{
	    if( shmctl( x->id, IPC_STAT, &blah ) == 0 )
	    {
		shmctl( x->id, IPC_RMID, &blah );
	    }
	    else
	    {
		//free( x->native_address );
	        VirtualFree((LPVOID)mem, 0, os_winnt ? MEM_RESET : (MEM_DECOMMIT | MEM_RELEASE));
	    }
	}
	x = x->next;
    }
}

static key_t get_next_shmkey( void )
{
    key_t result = -1;
    int i;
    for( i = 0; i < MAX_SHMID; i++ )
    {
	if( shmids[i].key == -1 )
	{
	    shmids[i].key = i;
	    result = i;
	    break;
	}
    }
    return result;
}

STATIC_INLINE key_t find_shmkey( key_t key )
{
    int result = -1;
    if( shmids[key].key == key )
    {
	result = key;
    }
    return result;
}

int mprotect(void *addr, size_t len, int prot)
{
    int result = 0;
  
    return result;
}

void *shmat(int shmid, void *shmaddr, int shmflg)
{ 
    void *result = (void *)-1;
    BOOL got = FALSE;
	
#ifdef NATMEM_OFFSET
    unsigned int size=shmids[shmid].size;
    extern uae_u32 allocated_z3fastmem;
    if(shmids[shmid].attached )
	return shmids[shmid].attached;
    if ((uae_u8*)shmaddr<natmem_offset){
	if(!strcmp(shmids[shmid].name,"chip"))
	{
	    shmaddr=natmem_offset;
	    got = TRUE;
//	    if(!currprefs.fastmem_size)
//		size+=32;
	}
	if(!strcmp(shmids[shmid].name,"kick"))
	{
	    shmaddr=natmem_offset+0xf80000;
	    got = TRUE;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"rom_e0"))
	{
	    shmaddr=natmem_offset+0xe00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"rom_f0"))
	{
	    shmaddr=natmem_offset+0xf00000;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"rtarea"))
	{
	    shmaddr=natmem_offset+RTAREA_BASE;
	    got = TRUE;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"fast"))
	{
	    shmaddr=natmem_offset+0x200000;
	    got = TRUE;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"z3")) {
	    shmaddr=natmem_offset+0x10000000;
	    if (allocated_z3fastmem<0x1000000)
	        gfxoffs=0x1000000;
	    else
		gfxoffs=allocated_z3fastmem;
	    got = TRUE;
	}
	if(!strcmp(shmids[shmid].name,"gfx"))
	{
	    shmaddr=natmem_offset+0x10000000+gfxoffs;
	    got = TRUE;
	    size+=32;
	    result=malloc(size);
	    shmids[shmid].attached=result;
	    return result;
	}
	if(!strcmp(shmids[shmid].name,"bogo"))
	{
	    shmaddr=natmem_offset+0x00C00000;
	    got = TRUE;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"filesys"))
	{
	    result=natmem_offset+0x10000;
	    shmids[shmid].attached=result;
	    return result;
	}
	if(!strcmp(shmids[shmid].name,"arcadia"))
	{
	    result=natmem_offset+0x10000;
	    shmids[shmid].attached=result;
	    return result;
	}
}
#endif
    
    if ((shmids[shmid].key == shmid) && shmids[shmid].size) {
	got = FALSE;
	if (got == FALSE) {
	    if (shmaddr)
		VirtualFree(shmaddr, 0, os_winnt ? MEM_RESET : MEM_RELEASE);
	    result = VirtualAlloc(shmaddr, size, os_winnt ? MEM_COMMIT : (MEM_RESERVE | MEM_COMMIT),
		PAGE_EXECUTE_READWRITE);
	    if (result == NULL) {
		result = (void*)-1;
		write_log ("VirtualAlloc %p-%p %x (%dk) failed %d\n", shmaddr, (uae_u8*)shmaddr + size,
		    size, size >> 10, GetLastError());
	    } else {
		shmids[shmid].attached = result; 
		write_log ("VirtualAlloc %p-%p %x (%dk) ok\n", shmaddr, (uae_u8*)shmaddr + size, size, size >> 10);
	    }
	} else {
	    shmids[shmid].attached = shmaddr;
	    result = shmaddr;
	}
    }
    return result;
}

int shmdt(const void *shmaddr)
{
    return 0;
}

int shmget(key_t key, size_t size, int shmflg, char *name)
{
    int result = -1;

    if( ( key == IPC_PRIVATE ) ||
	( ( shmflg & IPC_CREAT ) && ( find_shmkey( key ) == -1) ) )
    {
	write_log( "shmget of size %d (%dk) for %s\n", size, size >> 10, name );
	if( ( result = get_next_shmkey() ) != -1 )
    {
		
        //blah = VirtualAlloc( 0, size,MEM_COMMIT, PAGE_EXECUTE_READWRITE );

		shmids[result].size = size;
		strcpy( shmids[result].name, name );
	    }
		
	    else
	    {
		result = -1;
	    }
    }
    return result;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    int result = -1;

    if( ( find_shmkey( shmid ) != -1 ) && buf )
    {
	switch( cmd )
	{
	    case IPC_STAT:
		*buf = shmids[shmid];
		result = 0;
	    break;
	    case IPC_RMID:
		shmids[shmid].key = -1;
		shmids[shmid].name[0] = '\0';
		shmids[shmid].size = 0;
		result = 0;
	    break;
	}
    }
    return result;
}

int isinf( double x )
{
#ifdef _MSC_VER
    const int nClass = _fpclass(x);
    int result;
    if (nClass == _FPCLASS_NINF || nClass == _FPCLASS_PINF)  result = 1;
    else
 result = 0;
#else
    int result = 0;
#endif

    return result;
}

int isnan( double x )
{
#ifdef _MSC_VER
    int result = _isnan(x);
#else
    int result = 0;
#endif

    return result;
}

#endif
