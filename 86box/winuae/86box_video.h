/*
* UAE - The Un*x Amiga Emulator
*
* Interface between gfxboard.cpp and the 86Box video cards.
*/

#ifndef WINUAE_86BOX_VIDEO_H
#define WINUAE_86BOX_VIDEO_H

#include <stdint.h>

struct _device_;
typedef struct _device_ device_t;

/* Boards gfxboard.cpp offers, in the 86Box card sources */
extern const device_t gd5426_device;
extern const device_t gd5426_swapped_device;
extern const device_t gd5428_device;
extern const device_t gd5428_swapped_device;
extern const device_t gd5434_vlb_device;
extern const device_t gd5434_vlb_swapped_device;
extern const device_t gd5446_device;
extern const device_t s3_cybervision_trio64_device;
extern const device_t s3_trio64_device;
extern const device_t s3_virge_device;
extern const device_t permedia2_device;
extern const device_t ncr_retina_z2_device;
extern const device_t ncr_retina_z3_device;
extern const device_t et4000w32_merlin_z2_device;
extern const device_t et4000w32_merlin_z3_device;
extern const device_t et4000w32_omnibus_device;
extern const device_t et4000_domino_device;
extern const device_t et4000_omnibus_device;
extern const device_t inmos_egs_110_24_device;
extern const device_t inmos_visiona_z2_device;
extern const device_t inmos_rainbow3_z3_device;
extern const device_t voodoo_3_3000_device;
extern const device_t millennium_device;
extern const device_t millennium_ii_device;
extern const device_t mystique_device;
extern const device_t mystique_220_device;

/* Set up the video glue for board 'gfxboard' (rtggfxboard) and start the
   card. swapped: the board shows 15/16 bit pixels with red and blue
   swapped. Returns the card's private data, *svga gets its svga_t. */
void *pcem_device_init(const device_t *dev, void *gfxboard, bool swapped, void **svga);
/* Stop the card and the devices it added. */
void pcem_device_close(const device_t *dev, void *priv);
void pcem_device_force_redraw(const device_t *dev, void *priv);

/* Run one scanline, returns 1 at the start of vertical sync */
int svga_poll_eod(void *svga);
int svga_get_vtotal(void *svga);
bool svga_on(void *svga);
void svga_setvram(void *svga, uint8_t *vram);
uint8_t pcem_read_io(int port, int index);
/* The Voodoo 3's VRAM moved */
void voodoo_update_vram(void *banshee);

/* Timers of the card, called once per Amiga horizontal sync */
void pcemglue_hsync(void);

/* Board memory, I/O and PCI configuration space accesses, size 0 = byte,
   1 = word, 2 = long */
void put_io_pcem(uint32_t addr, uint32_t v, int size);
uint32_t get_io_pcem(uint32_t addr, int size);
void put_mem_pcem(uint32_t addr, uint32_t v, int size);
uint32_t get_mem_pcem(uint32_t addr, int size);
void put_pci_pcem(uint32_t addr, uint8_t v);
uint8_t get_pci_pcem(uint32_t addr);

/* Rendered line yy of the frame that starts at x, y */
uint8_t *getpcembuffer32(int x, int y, int yy);
/* Mark VRAM the Amiga wrote directly as changed */
void pcem_linear_mark(int offset);

/* The card's linear frame buffer handlers (the first mapping of 1MB or
   more), for direct calls from the Voodoo 3 PCI board. */
extern void *pcem_mapping_linear_priv;
extern uint32_t pcem_mapping_linear_offset;
extern uint8_t (*pcem_linear_read_b)(uint32_t addr, void *priv);
extern uint16_t (*pcem_linear_read_w)(uint32_t addr, void *priv);
extern uint32_t (*pcem_linear_read_l)(uint32_t addr, void *priv);
extern void (*pcem_linear_write_b)(uint32_t addr, uint8_t val, void *priv);
extern void (*pcem_linear_write_w)(uint32_t addr, uint16_t val, void *priv);
extern void (*pcem_linear_write_l)(uint32_t addr, uint32_t val, void *priv);

/* In gfxboard.cpp */
void gfxboard_resize(int width, int height, int hmult, int vmult, void *p);
void gfxboard_intreq(void *p, int act, bool safe);
void video_blit_memtoscreen(int x, int y, int y1, int y2, int w, int h);
void *pcem_getvram(int size);
int pcem_getvramsize(void);

#endif /* WINUAE_86BOX_VIDEO_H */
