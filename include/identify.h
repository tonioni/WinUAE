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
    uae_u8 rw, special;
};

/*
 special:

 1: DMA pointer high word
 2: DMA pointer low word
 4: ECS/AGA only
 8: AGA only
*/

extern const struct mem_labels mem_labels[];
extern const struct mem_labels int_labels[];
extern const struct mem_labels trap_labels[];
extern const struct customData custd[];

