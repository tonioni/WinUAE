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

static struct shmid_ds shmids[ MAX_SHMID ];
static uae_u32 gfxoffs;

uae_u32 natmem_offset = 0;

void init_shm( void )
{
    int i;
    LPVOID blah = NULL;
#if 0
    LPBYTE address = NULL; // Let the system decide where to put the memory...
#else
    LPBYTE address = (LPBYTE)0x10000000; // Letting the system decide doesn't seem to work on some systems
#endif

    canbang = 0;
    gfxoffs = 0;
    shm_start = 0;
    for( i = 0; i < MAX_SHMID; i++ )
    {
	shmids[i].attached = 0;
	shmids[i].key = -1;
	shmids[i].size = 0;
	shmids[i].addr = NULL;
	shmids[i].name[0] = 0;
    }
    while( address < (LPBYTE)0xa0000000 )
    {
        blah = VirtualAlloc( address, 0x19000000, MEM_RESERVE, PAGE_EXECUTE_READWRITE );
        if( blah == NULL )
        {
	    address += 0x01000000;
	}
        else
        {
	    natmem_offset = (uae_u32)blah + 0x1000000;
	    break;
	}
    }
    if( natmem_offset )
    {
    	write_log( "NATMEM: Our special area is 0x%x\n", natmem_offset );
	VirtualFree( blah, 0, MEM_RELEASE );
	canbang = 1;
    }
    else
    {
	write_log( "NATMEM: No special area could be allocated!\n" );
    }
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
	        VirtualFree((LPVOID)mem, 0, MEM_DECOMMIT |MEM_RELEASE );
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

void *shmat(int shmid, LPVOID shmaddr, int shmflg)
{ 
    void *result = (void *)-1;
    BOOL got = FALSE;
	
#ifdef NATMEM_OFFSET
    int size=shmids[shmid].size;
    extern uae_u32 allocated_z3fastmem;
    if(shmids[shmid].attached )
	return shmids[shmid].attached;
    if (shmaddr<natmem_offset){
	if(!strcmp(shmids[shmid].name,"chip"))
	{
	    shmaddr=natmem_offset;
	    got = TRUE;
	    if(!currprefs.fastmem_size)
		size+=32;
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
}
#endif
    
    if( ( shmids[shmid].key == shmid ) && shmids[shmid].size )
    {
	got = FALSE;
	if (got == FALSE) {
	    if (shmaddr)
	    {
		result=(void*)VirtualFree(shmaddr,0,MEM_RELEASE);
	    }
	    result =VirtualAlloc(shmaddr,size,MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	    if( result == NULL )
	    {
		result = (void *)-1;
	    }
	    else
	    {
		shmids[shmid].attached=result; 
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
	write_log( "shmget of size %d for %s\n", size, name );
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

