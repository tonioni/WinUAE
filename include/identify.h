 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Tables for labelling amiga internals. 
  *
  */
		
struct mem_labels
{
    const char *name;
    uae_u32 adr;
};

struct customData
{
    const char *name;
    uae_u32 adr;
};


extern struct mem_labels mem_labels[];
extern struct mem_labels int_labels[];
extern struct mem_labels trap_labels[];
extern struct customData custd[];

