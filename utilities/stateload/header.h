
#define TEMP_STACK_SIZE 8000

#define ALLOCATIONS 256

struct Allocation
{
	struct MemHeader *mh;
	UBYTE *addr;
	ULONG size;
};

struct MemoryBank
{
	UBYTE *addr;
	ULONG flags;
	UBYTE *targetaddr;
	ULONG size;
	ULONG targetsize;
	ULONG offset;
	UBYTE chunk[5];
};

// CHIP, SLOW, FAST
#define MEMORY_REGIONS 3
#define MB_CHIP 0
#define MB_SLOW 1
#define MB_FAST 2

#define MAPROM_ACA500 1
#define MAPROM_ACA500P 2
#define MAPROM_ACA1221EC 3
#define MAPROM_ACA12xx 4
#define MAPROM_GVP 5
#define MAPROM_BLIZZARD12x0 6
#define MAPROM_ACA1233N 7
#define MAPROM_MMU 255

#define FLAGS_NOCACHE 1
#define FLAGS_FORCEPAL 2
#define FLAGS_FORCENTSC 4

struct mapromdata
{
	UWORD type;
	ULONG config;
	ULONG addr;
	APTR board;
	ULONG memunavailable;
};

struct uaestate
{
	ULONG flags;
	UBYTE *cpu_chunk;
	UBYTE *ciaa_chunk, *ciab_chunk;
	UBYTE *custom_chunk;
	UBYTE *aga_colors_chunk;
	UBYTE *floppy_chunk[4];
	UBYTE *audio_chunk[4];
	UBYTE *sprite_chunk[8];
	ULONG *MMU_Level_A;

	UBYTE *maprom;
	ULONG mapromsize;
	ULONG maprom_memlimit;
	struct mapromdata mrd[2];

	UBYTE *extra_ram;
	ULONG extra_ram_size;
	ULONG errors;

	struct MemHeader *mem_allocated[MEMORY_REGIONS];
	struct MemHeader *extra_mem_head;
	UBYTE *extra_mem_pointer;
	struct MemoryBank membanks[MEMORY_REGIONS];

	int num_allocations;
	struct Allocation allocations[ALLOCATIONS];
	
	WORD mmutype;
	UBYTE *page_ptr;
	ULONG page_free;
	
	UWORD romver, romrev;
	UBYTE agastate;
	UBYTE usemaprom;
	UBYTE debug;
	UBYTE testmode;
	UBYTE nowait;
	UBYTE canusemmu;
	UBYTE mmuused;
};

UBYTE *allocate_abs(ULONG size, ULONG addr, struct uaestate *st);

BOOL map_region(struct uaestate *st, void *addr, void *physaddr, ULONG size, BOOL invalid, BOOL writeprotect, BOOL supervisor, UBYTE cachemode);
BOOL unmap_region(struct uaestate *st, void *addr, ULONG size);
BOOL init_mmu(struct uaestate *st);

