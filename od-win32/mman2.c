// Implement mprotect() for Win32
// Copyright (C) 2000, Brian King
// GNU Public License

#include "sysconfig.h" 
#include "sysdeps.h"
#include "sys/mman.h"
#include "include/memory.h"
#include "options.h"
#include <float.h>
#include "autoconf.h"

static struct shmid_ds shmids[ MAX_SHMID ];

uae_u32 natmem_offset = 0;

void init_shm( void )
{
    int i;
    LPTSTR string = NULL;
#if 0
    LPBYTE address = NULL; // Let the system decide where to put the memory...
#else
    LPBYTE address = (LPBYTE)0x10000000; // Letting the system decide doesn't seem to work on some systems
#endif

    for( i = 0; i < MAX_SHMID; i++ ) {
	shmids[i].attached = 0;
	shmids[i].key = -1;
	shmids[i].size = 0;
	shmids[i].addr = NULL;
	shmids[i].name[0] = 0;
    }
    natmem_offset = 0;
     while( address < (LPBYTE)0x20000000 ) {
	if (VirtualAlloc( address, 0x18800000, MEM_RESERVE, PAGE_EXECUTE_READWRITE )) {
	    natmem_offset = (uae_u32)address;
	    break;
	}
	address += 0x01000000;
    }
    if (natmem_offset) {
    	write_log( "NATMEM: Our special area is 0x%x\n", natmem_offset );
	canbang = 1;
	VirtualFree (natmem_offset, 0, MEM_RELEASE);
    } else {
	write_log( "NATMEM: No special area could be allocated!\n" );
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
    char *result = (void *)-1;
    BOOL locked = FALSE;
	
#ifdef NATMEM_OFFSET
    int size=shmids[shmid].size;
    extern uae_u32 allocated_z3fastmem;
    if(shmids[shmid].attached )
	return shmids[shmid].attached;
    if (shmaddr<natmem_offset){
	if(!strcmp(shmids[shmid].name,"chip"))
	{
	    shmaddr=natmem_offset;
	    if(!currprefs.fastmem_size)
		size+=32;
	}
	if(!strcmp(shmids[shmid].name,"kick"))
	{
	    shmaddr=natmem_offset+0xf80000;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"rom_e0"))
	{
	    shmaddr=natmem_offset+0xe00000;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"rom_f0"))
	{
	    shmaddr=natmem_offset+0xf00000;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"rtarea"))
	{
	    shmaddr=natmem_offset+RTAREA_BASE;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"fast"))
	{
	    shmaddr=natmem_offset+0x200000;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"z3")) {
	    shmaddr=natmem_offset+0x10000000;
	}
	if(!strcmp(shmids[shmid].name,"gfx"))
	{
	    shmaddr=natmem_offset+0x10000000+allocated_z3fastmem;
	    size+=32;
	}
	if(!strcmp(shmids[shmid].name,"bogo"))
	{
	    shmaddr=natmem_offset+0x00C00000;
	    size+=32;
	}
    }
#endif
    
    if( ( shmids[shmid].key == shmid ) && shmids[shmid].size )
    {
	if (shmaddr)
	{
	    result=VirtualFree(shmaddr,0,MEM_RELEASE);
	}
	
        result = VirtualAlloc(shmaddr,size,MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_READWRITE );
	if( result == NULL )
	{
	    result = (void *)-1;
	}
	else
	{
	    shmids[shmid].attached=result; 
	    //memset(result,0,size);
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
