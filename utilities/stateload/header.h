
#define TEMP_STACK_SIZE 8000

#define ALLOCATIONS 30

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

#define MAPROM_ACA500 (1<<0)
#define MAPROM_ACA500P (1<<1)
#define MAPROM_ACA1221EC (1<<2)
#define MAPROM_ACA12xx64 (1<<3)
#define MAPROM_ACA12xx128 (1<<4)

#define FLAGS_NOCACHE 1
#define FLAGS_FORCEPAL 2
#define FLAGS_FORCENTSC 4

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
	UBYTE *maprom;
	ULONG mapromsize;
	ULONG mapromtype;

	UBYTE *extra_ram;
	ULONG extra_ram_size;
	ULONG errors;

	struct MemHeader *mem_allocated[MEMORY_REGIONS];
	struct MemHeader *extra_mem_head;
	UBYTE *extra_mem_pointer;
	struct MemoryBank membanks[MEMORY_REGIONS];

	int num_allocations;
	struct Allocation allocations[ALLOCATIONS];
	ULONG memunavailable;
	
	UWORD romver, romrev;
	UBYTE agastate;
	UBYTE usemaprom;
	UBYTE debug;
	UBYTE testmode;
	UBYTE nowait;
};

