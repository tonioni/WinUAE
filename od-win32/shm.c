#include <float.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "sys/mman.h"
#include "include/memory.h"
#include "options.h"
#include "autoconf.h"

static void win32_error (const char *format,...)
{
    LPVOID lpMsgBuf;
    va_list arglist;
    char buf[1000];

    va_start (arglist, format );
    vsprintf (buf, format, arglist);
    FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM|FORMAT_MESSAGE_IGNORE_INSERTS,
	NULL,GetLastError(),MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	(LPTSTR) &lpMsgBuf,0,NULL);
    write_log ("%s: %s",buf , (char*)lpMsgBuf);
    va_end( arglist );
}

static struct shmid_ds shmids[ MAX_SHMID ];

uae_u32 natmem_offset;

void init_shm( void )
{
    int i;

#ifdef NATMEM_OFFSET
    uae_u32 addr;

    canbang = 1;
    for( i = 0; i < MAX_SHMID; i++ ) {
	shmids[i].attached = 0;
	shmids[i].key = -1;
	shmids[i].size = 0;
	shmids[i].addr = (void*)0xffffffff;
	shmids[i].name[0] = 0;
	shmids[i].filemapping = INVALID_HANDLE_VALUE;
    }
    addr = 0x10000000;
    while( addr < 0xa0000000 ) {
	if (VirtualAlloc( (LPVOID)addr, 0x18800000, MEM_RESERVE, PAGE_EXECUTE_READWRITE )) {
	    VirtualFree ( (LPVOID)addr, 0, MEM_RELEASE);
	    addr += 0x01000000;
	    natmem_offset = addr;
	    break;
	}
	addr += 0x01000000;
    }
    if (natmem_offset) {
	write_log ( "NATMEM: Our special area is 0x%x\n", natmem_offset);
    } else {
	canbang = 0;
    }
#endif
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

int shmget(key_t key, size_t size, int shmflg, char *name)
{
    int result = -1;

    if( ( key == IPC_PRIVATE ) || ( ( shmflg & IPC_CREAT ) && ( find_shmkey( key ) == -1) ) ) {
	write_log ( "shmget of size %d for %s\n", size, name );
	if( ( result = get_next_shmkey() ) != -1 ) {
		HANDLE h = CreateFileMapping (NULL, 0, PAGE_READWRITE, 0, size, NULL);
		if (h == NULL)
		    win32_error ("CreateFileMapping %d\n", size);
		shmids[result].filemapping = h;
		shmids[result].size = size;
		strcpy( shmids[result].name, name );
	} else {
	    result = -1;
	}
    }
    return result;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
    int result = -1;

    return result;
    if( ( find_shmkey( shmid ) != -1 ) && buf ) {
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
	    CloseHandle(shmids[shmid].filemapping);
	    shmids[shmid].filemapping = INVALID_HANDLE_VALUE;
	    result = 0;
	    break;
	}
    }
    return result;
}

void *shmat(int shmid, LPVOID shmaddr, int shmflg)
{
    struct shmid_ds *shm = &shmids[shmid];

    if(shm->addr == shmaddr )
	return shm->addr;
    shm->addr = MapViewOfFileEx (shm->filemapping, FILE_MAP_WRITE, 0, 0, shm->size, shmaddr);
    if (addr == NULL)
	win32_error("MapViewOfFileEx %08X", shmaddr);
    write_log ("shmat %08X -> %08X\n", shmaddr, shm->addr);
    shm->attached = 1;
    return shm->addr;
}

int shmdt(const void *shmaddr)
{
    int i;
    if (shmaddr == (void*)0xffffffff)
	return 0;
    write_log ("shmdt: %08X\n", shmaddr);
    if (UnmapViewOfFile ((LPCVOID)shmaddr) == FALSE) {
	win32_error("UnmapViewOfFile %08X", shmaddr);
	return 0;
    }
    for( i = 0; i < MAX_SHMID; i++ ) {
	struct shmid_ds *shm = &shmids[i];
	if (shm->addr == shmaddr) {
	    shm->addr = (void*)0xffffffff;
	}
    }
    return -1;
}