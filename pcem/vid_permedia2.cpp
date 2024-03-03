
/*

Preliminary Permedia 2 (Amiga CyberVision/BlizzardVision PPC) emulation by Toni Wilen 2024

Emulated:

- Standard Graphics processor modes supported (8/15/16/24/32 bits). Other weird modes are not supported.
- VRAM aperture byte swapping and RAMDAC red/blue swapping modes. (Used at least by Picasso96 driver)
- Hardware cursor.

Not emulated but planned:

- 2D blitter. NOBLITTER=YES required in Picasso96 tool types
- Overlay

Not emulated and Someone Else's Problem:

- SVGA core. Amiga programs seem to always use Graphics processor mode.
- 3D. 3D is someone else's problem. (Maybe some PCem or x86Box developer is interested?)
- Other Permedia 2 special features (front/back buffer swapping, stereo support etc)

*/

#include <stdlib.h>
#include "ibm.h"
#include "device.h"
#include "io.h"
#include "pci.h"
#include "mem.h"
#include "rom.h"
#include "thread.h"
#include "video.h"
#include "vid_permedia2.h"
#include "vid_svga.h"
#include "vid_svga_render.h"
#include "vid_sdac_ramdac.h"

extern void activate_debugger(void);

typedef struct permedia2_t
{
        mem_mapping_t linear_mapping[2];
        mem_mapping_t mmio_mapping;
        
        rom_t bios_rom;
        
        svga_t svga;
        sdac_ramdac_t ramdac;
        uint8_t pci_regs[256];
        int card;

        uint8_t ma_ext;

        int chip;
        uint8_t int_line;
        uint8_t id_ext_pci;

        uint32_t linear_base[2], linear_size;
        uint32_t mmio_base, mmio_size;

        uint32_t intena, intreq;

        uint32_t bank[4];
        uint32_t vram_mask;
        
        float (*getclock)(int clock, void *p);
        void *getclock_p;

        int vblank_irq;

        uint8_t ramdac_reg;
        uint8_t ramdac_pal, ramdac_palcomp;
        uint8_t ramdac_cpal, ramdac_cpalcomp;
        uint8_t ramdac_vals[256];
        uint32_t vc_regs[256];
        uint32_t ramdac_hwc_col[4];
        uint16_t ramdac_cramaddr;
        uint8_t ramdac_cram[1024];

        uint8_t linear_byte_control[2];
        
} permedia2_t;

static __inline uint32_t do_byteswap_32(uint32_t v)
{
    return _byteswap_ulong(v);
}

void permedia2_updatemapping(permedia2_t*);
void permedia2_updatebanking(permedia2_t*);

static int permedia2_vsync_enabled(permedia2_t *permedia2)
{
    if (permedia2->intena & 0x10) {
        return 1;
    }
    return 0;
}

static void permedia2_update_irqs(permedia2_t *permedia2)
{
    if (permedia2->vblank_irq > 0 && permedia2_vsync_enabled(permedia2)) {
        permedia2->intreq |= 0x10;
        pci_set_irq(NULL, PCI_INTA);
    } else {
        pci_clear_irq(NULL, PCI_INTA);
    }
}

static void permedia2_vblank_start(svga_t *svga)
{
    permedia2_t *permedia2 = (permedia2_t *)svga->p;
    if (permedia2->vblank_irq >= 0) {
        permedia2->vblank_irq = 1;
        permedia2_update_irqs(permedia2);
    }
}



void permedia2_out(uint16_t addr, uint8_t val, void *p)
{
        permedia2_t *permedia2 = (permedia2_t *)p;
        svga_t *svga = &permedia2->svga;
        uint8_t old;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;
        
        switch (addr)
        {
                case 0x3C6:
                    val &= ~0x10;
                    sdac_ramdac_out((addr & 3) | 4, val, &permedia2->ramdac, svga);
                    return;
                case 0x3C7: case 0x3C8: case 0x3C9:
                    sdac_ramdac_out(addr & 3, val, &permedia2->ramdac, svga);
                    return;

                case 0x3c4:
                svga->seqaddr = val & 0x3f;
                break;
                case 0x3c5:
                {
                    old = svga->seqregs[svga->seqaddr];
                    svga->seqregs[svga->seqaddr] = val;
                    if (old != val && svga->seqaddr >= 0x0a) {
                        svga->fullchange = changeframecount;
                    }
                    switch (svga->seqaddr)
                    {
                        default:
                        break;
                    }
                    if (old != val)
                    {
                        svga->fullchange = changeframecount;
                        svga_recalctimings(svga);
                    }
                }
                break;

                case 0x3d4:
                svga->crtcreg = val & svga->crtcreg_mask;
                return;
                case 0x3d5:
                {
                    old = svga->crtc[svga->crtcreg];
                    svga->crtc[svga->crtcreg] = val;
                    switch (svga->crtcreg)
                    {
                            case 0x11:
                            if (!(val & 0x10)) {
                                permedia2->vblank_irq = 0;
                            }
                            permedia2_update_irqs(permedia2);
                            if ((val & ~0x30) == (old & ~0x30))
                                old = val;
                            break;
                    }
                    if (old != val)
                    {
                            if (svga->crtcreg < 0xe || svga->crtcreg > 0x10)
                            {
                                    svga->fullchange = changeframecount;
                                    svga_recalctimings(svga);
                            }
                    }
                }
                break;
        }
        svga_out(addr, val, svga);
}

uint8_t permedia2_in(uint16_t addr, void *p)
{
        permedia2_t *permedia2 = (permedia2_t *)p;
        svga_t *svga = &permedia2->svga;
        uint8_t ret;

        if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1)) 
                addr ^= 0x60;

        switch (addr)
        {
                case 0x3c2:
                ret = svga_in(addr, svga);
                ret |= permedia2->vblank_irq > 0 ? 0x80 : 0x00;
                return ret;
                case 0x3c5:
                switch(svga->seqaddr)
                {
                    default:
                    break;
                }
                if (svga->seqaddr >= 0x08 && svga->seqaddr < 0x40)
                        return svga->seqregs[svga->seqaddr];
                break;

                case 0x3d4:
                return svga->crtcreg;
                case 0x3d5:
                return svga->crtc[svga->crtcreg];
        }
        return svga_in(addr, svga);
}

void permedia2_recalctimings(svga_t *svga)
{
        permedia2_t *permedia2 = (permedia2_t*)svga->p;
        bool svga_mode = (svga->seqregs[0x05] & 0x08) != 0;
        int bpp = 8;

        if (svga_mode) {



        } else {
            // graphics processor mode
            svga->ma_latch = permedia2->vc_regs[0 / 4] & 0xfffff;
            svga->rowoffset = permedia2->vc_regs[8 / 4] & 0xfffff;
            svga->htotal = (((permedia2->vc_regs[0x10 / 4] & 2047) - (permedia2->vc_regs[0x20 / 4] & 2047)) + 1) * 4;
            svga->vtotal = permedia2->vc_regs[0x38 / 4] & 2047;
            svga->vsyncstart = svga->vtotal;
            svga->dispend = svga->vtotal - (permedia2->vc_regs[0x40 / 4] & 2047) + 1;
            svga->hdisp = svga->htotal;
            svga->split = 99999;
            svga->vblankstart = svga->vtotal;

            svga->crtc[0x17] |= 0x80;
            svga->gdcreg[6] |= 1;

            svga->video_res_override = 1;

            switch(permedia2->ramdac_vals[0x18] & 15)
            {
                case 0:
                default:
                    bpp = 8;
                    break;
                case 4:
                    bpp = 15;
                    break;
                case 6:
                    bpp = 16;
                    break;
                case 8:
                    bpp = 32;
                    svga->hdisp /= 2;
                    break;
                case 9:
                    bpp = 24;
                    svga->hdisp *= 2;
                    svga->hdisp /= 3;
                    break;
            }
            svga->video_res_x = svga->hdisp;
            svga->video_res_y = svga->dispend;

            svga->bpp = bpp;
            svga->video_bpp = bpp;

            bool oswaprb = svga->swaprb;
            svga->swaprb = (permedia2->ramdac_vals[0x18] & 0x20) == 0;
            if (oswaprb != svga->swaprb) {
                void pcemvideorbswap(bool swapped);
                pcemvideorbswap(svga->swaprb);
            }
        }


        switch (bpp)
        {
            case 4:
                svga->render = svga_render_4bpp_highres;
                break;
            case 8:
                svga->render = svga_render_8bpp_highres;
                break;
            case 15:
                svga->render = svga_render_15bpp_highres;
                break;
            case 16:
                svga->render = svga_render_16bpp_highres;
                break;
            case 24:
                svga->render = svga_render_24bpp_highres;
                break;
            case 32:
                svga->render = svga_render_32bpp_highres;
                break;
        }

        svga->horizontal_linedbl = svga->dispend * 9 / 10 >= svga->hdisp;
}

static void permedia2_hwcursor_draw(svga_t *svga, int displine)
{
    permedia2_t *permedia2 = (permedia2_t*)svga->p;
    int addr = svga->hwcursor_latch.addr;
    int addradd = 0;
    uint8_t dat[2];
    int offset = svga->hwcursor_latch.x;
    uint8_t control = permedia2->ramdac_vals[6];
    int cmode = control & 3;

    offset <<= svga->horizontal_linedbl;

    if (svga->hwcursor.xsize == 32) {
        addradd = ((control >> 4) & 3) * (32 * 32 / 8);
    }

    for (int x = 0; x < svga->hwcursor.xsize; x += 8)
    {
        dat[0] = permedia2->ramdac_cram[addr + addradd];
        dat[1] = permedia2->ramdac_cram[addr + 0x200 + addradd];
        for (int xx = 0; xx < 8; xx++)
        {
            if (offset >= 0) {
                int cidx = 0;
                int comp = 0;
                int cval = ((dat[1] & 0x80) ? 2 : 0) | ((dat[0] & 0x80) ? 1 : 0);
                if (cmode == 1) {
                    cidx = cval;
                } else if (cmode == 2) {
                    if (cval == 0 || cval == 1) {
                        cidx = cval + 1;
                    } else if (cval == 3) {
                        comp = 1;
                    }
                } else {
                    cidx = cval - 1;
                }

                if (comp) {
                    ((uint32_t*)buffer32->line[displine])[offset + 32] ^= 0xffffff;
                } else if (cidx > 0) {
                    ((uint32_t*)buffer32->line[displine])[offset + 32] = permedia2->ramdac_hwc_col[cidx];
                }
            }

            offset++;
            dat[0] <<= 1;
            dat[1] <<= 1;
        }
        addr++;
    }
    svga->hwcursor_latch.addr += svga->hwcursor.xsize / 8;

}


static void permedia2_write(uint32_t addr, uint8_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = changeframecount;
    *(uint8_t *)&svga->vram[addr] = val;
}
static void permedia2_writew(uint32_t addr, uint16_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = changeframecount;
    *(uint16_t *)&svga->vram[addr] = val;
}
static void permedia2_writel(uint32_t addr, uint32_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return;
    addr &= svga->vram_mask;
    svga->changedvram[addr >> 12] = changeframecount;
    *(uint32_t *)&svga->vram[addr] = val;
}

static uint8_t permedia2_read(uint32_t addr, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xff;

    return *(uint8_t *)&svga->vram[addr & svga->vram_mask];
}
static uint16_t permedia2_readw(uint32_t addr, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffff;
    return *(uint16_t *)&svga->vram[addr & svga->vram_mask];
}
static uint32_t permedia2_readl(uint32_t addr, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    addr = (addr & 0xffff) + permedia2->bank[(addr >> 15) & 3];

    addr &= svga->decode_mask;
    if (addr >= svga->vram_max)
        return 0xffff;

    return *(uint32_t *)&svga->vram[addr & svga->vram_mask];
}

static void permedia2_ramdac_write(int reg, uint8_t v, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;

    if (reg == 0) { // palettewriteaddress
        permedia2->ramdac_reg = v;
        permedia2->ramdac_pal = v;
        permedia2->ramdac_palcomp = 0;
        permedia2->ramdac_cramaddr &= 0x0300;
        permedia2->ramdac_cramaddr |= v;
    } else if (reg == 8) { // palettedata
        int idx = permedia2->ramdac_pal;
        int comp = permedia2->ramdac_palcomp;
        if (!comp) {
            svga->vgapal[idx].r = v;
        } else if (comp == 1) {
            svga->vgapal[idx].g = v;
        } else {
            svga->vgapal[idx].b = v;
        }
        svga->pallook[idx] = makecol32(svga->vgapal[idx].r, svga->vgapal[idx].g, svga->vgapal[idx].b);
        permedia2->ramdac_palcomp++;
        if (permedia2->ramdac_palcomp >= 3) {
            permedia2->ramdac_palcomp = 0;
            permedia2->ramdac_pal++;
        }
    } else if (reg == 0x20) { // cursorcoloraddress
        permedia2->ramdac_cpal = v;
        permedia2->ramdac_cpalcomp = 0;
    } else if (reg == 0x28) { // cursorcolordata
        int idx = permedia2->ramdac_cpal;
        int comp = permedia2->ramdac_cpalcomp;
        if (!comp) {
            permedia2->ramdac_hwc_col[idx] &= 0x00ffff;
            permedia2->ramdac_hwc_col[idx] |= v << 16;
        } else if (comp == 1) {
            permedia2->ramdac_hwc_col[idx] &= 0xff00ff;
            permedia2->ramdac_hwc_col[idx] |= v << 8;
        } else {
            permedia2->ramdac_hwc_col[idx] &= 0xffff00;
            permedia2->ramdac_hwc_col[idx] |= v << 0;
        }
        permedia2->ramdac_cpalcomp++;
        if (permedia2->ramdac_cpalcomp >= 3) {
            permedia2->ramdac_cpalcomp = 0;
            permedia2->ramdac_cpal++;
            permedia2->ramdac_cpal &= 3;
        }
    } else if (reg == 0x50) { // indexeddata
        permedia2->ramdac_vals[permedia2->ramdac_reg] = v;
        if (permedia2->ramdac_reg == 0x06) { // cursorcontrol
            svga->hwcursor.ena = (v & 3) != 0;
            svga->hwcursor.ysize = svga->hwcursor.xsize = (v & 0x40) ? 64 : 32;
            permedia2->ramdac_cramaddr &= 0x00ff;
            permedia2->ramdac_cramaddr |= ((v >> 2) & 3) << 8;
        } else if (permedia2->ramdac_reg == 0x18) { // colormode
            svga_recalctimings(&permedia2->svga);
        }
    } else if (reg == 0x58) { // cursorramdata
        permedia2->ramdac_cram[permedia2->ramdac_cramaddr] = v;
        permedia2->ramdac_cramaddr++;
        permedia2->ramdac_cramaddr &= 1023;
    } else if (reg == 0x60) { // cursorxlow
        svga->hwcursor.x += 64;
        svga->hwcursor.x &= 0xff00;
        svga->hwcursor.x |= v;
        svga->hwcursor.x -= 64;
    } else if (reg == 0x68) { // cursorxhigh
        svga->hwcursor.x += 64;
        svga->hwcursor.x &= 0x00ff;
        svga->hwcursor.x |= v << 8;
        svga->hwcursor.x -= 64;
    } else if (reg == 0x70) { // cursorylow
        svga->hwcursor.y += 64;
        svga->hwcursor.y &= 0xff00;
        svga->hwcursor.y |= v;
        svga->hwcursor.y -= 64;
    } else if (reg == 0x78) { // cursoryhigh
        svga->hwcursor.y += 64;
        svga->hwcursor.y &= 0x00ff;
        svga->hwcursor.y |= v << 8;
        svga->hwcursor.y -= 64;
    }
}

static uint32_t permedia2_ramdac_read(int reg, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;
    uint32_t v = 0;

    if (reg == 0) {
        // address register
        v = permedia2->ramdac_reg;
    } else if (reg == 0x18) { // palettereadaddress
        int idx = permedia2->ramdac_pal;
        int comp = permedia2->ramdac_palcomp;
        if (!comp) {
            v = svga->vgapal[idx].r;
        } else if (comp == 1) {
            v = svga->vgapal[idx].g;
        } else {
            v = svga->vgapal[idx].b;
        }
        permedia2->ramdac_palcomp++;
        if (permedia2->ramdac_palcomp >= 3) {
            permedia2->ramdac_palcomp = 0;
            permedia2->ramdac_pal++;
        }
    } else if (reg == 0x50) {
        // data register
        v = permedia2->ramdac_vals[permedia2->ramdac_reg];
        switch (permedia2->ramdac_reg)
        {
            case 0x29: // pixelclockstatus
                v = 0x10; // PLL locked
                break;
            case 0x33: // memoryclockstatus
                v = 0x10; // PLL locked
                break;
        }
    }

    return v;
}

static void permedia2_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t*)p;
    pclog("PM2 MMIO WRITEB %08x -> %02x\n", addr, val);
}

static void permedia2_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t*)p;
    pclog("PM2 MMIO WRITEW %08x -> %04x\n", addr, val);
}

static void permedia2_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t*)p;
    int reg = addr & 0xffff;

    if (addr & 0x10000) {
        val = do_byteswap_32(val);
    }
    if (reg >= 0x4000 && reg < 0x5000) {
        permedia2_ramdac_write(reg & 0xff, val, p);
    } else if (reg >= 0x3000 && reg < 0x4000) {
        int vcreg = reg & 0xff;
        permedia2->vc_regs[vcreg / 4] = val;
        svga_recalctimings(&permedia2->svga);
    } else if (reg < 0x2000) {
        switch(reg)
        {
            case 0x08:
            permedia2->intena = val;
            break;
            case 0x10:
            if (val & 0x10) {
                permedia2->vblank_irq = 0;
            }
            permedia2->intreq &= ~val;
            break;
            case 0x50:
            //pclog("Aperture 1: %02x\n", val);
            permedia2->linear_byte_control[0] = val & 3;
            break;
            case 0x58:
            //pclog("Aperture 2: %02x\n", val);
            permedia2->linear_byte_control[1] = val & 3;
            break;
        }
        //pclog("control write %08x = %08x\n", addr, val);
    } else {
        pclog("Unsupported PM2 MMIO Write %08x = %08x\n", addr, val);
    }
}

static uint32_t permedia2_mmio_readl(uint32_t addr, void *p)
{
    permedia2_t *permedia2 = (permedia2_t*)p;
    uint32_t v = 0;
    int reg = (addr & 0xffff);

    if (reg >= 0x4000 && reg < 0x5000) {
        v = permedia2_ramdac_read(reg & 0xff, p);
    } else if (reg >= 0x3000 && reg < 0x4000) {
        int vcreg = reg & 0xff;
        v = permedia2->vc_regs[vcreg / 4];
    } else if (reg < 0x2000) {
        switch (reg)
        {
            case 0x08:
            v = permedia2->intena;
            break;
            case 0x10:
            v = permedia2->intreq;
            break;
            case 0x18:
            v = 0x100;
            break;
        }
        //pclog("control read %08x = %08x\n", addr, v);
    } else {
        pclog("Unsupported PM2 MMIO Read %08x\n", addr);
    }
    if (addr & 0x10000) {
        v = do_byteswap_32(v);
    }
    return v;
}

static uint16_t permedia2_mmio_readw(uint32_t addr, void *p)
{
    uint32_t v = permedia2_mmio_readl(addr, p);
    if (addr & 2) {
        v >>= 16;
    }
    return v;
}
static uint8_t permedia2_mmio_read(uint32_t addr, void *p)
{
    uint32_t v = permedia2_mmio_readl(addr, p);
    if (addr & 2) {
        v >>= 16;
    }
    if (addr & 1) {
        v >>= 8;
    }
    return v;
}

static uint8_t permedia2_read_linear1(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    uint8_t *fbp = (uint8_t*)(&svga->vram[addr & svga->vram_mask]);
    uint8_t v = *fbp;
    return v;
}
static uint16_t permedia2_readw_linear1(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    uint16_t *fbp = (uint16_t*)(&svga->vram[addr & svga->vram_mask]);
    uint16_t v = *fbp;

    if (permedia2->linear_byte_control[0] == 2) {
        v = (v >> 8) | (v << 8);
    }

    return v;
}
static uint32_t permedia2_readl_linear1(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    uint32_t *fbp = (uint32_t*)(&svga->vram[addr & svga->vram_mask]);
    uint32_t v = *fbp;

    if (permedia2->linear_byte_control[0] == 2) {
        v = do_byteswap_32(v);
        v = (v >> 16) | (v << 16);
    } else if (permedia2->linear_byte_control[0] == 1) {
        v = do_byteswap_32(v);
    }

    return v;
}

static uint8_t permedia2_read_linear2(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    uint8_t *fbp = (uint8_t *)(&svga->vram[addr & svga->vram_mask]);
    uint8_t v = *fbp;
    return v;
}
static uint16_t permedia2_readw_linear2(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    uint16_t *fbp = (uint16_t *)(&svga->vram[addr & svga->vram_mask]);
    uint16_t v = *fbp;

    if (permedia2->linear_byte_control[1] == 2) {
        v = (v >> 8) | (v << 8);
    }

    return v;
}
static uint32_t permedia2_readl_linear2(uint32_t addr, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    uint32_t *fbp = (uint32_t *)(&svga->vram[addr & svga->vram_mask]);
    uint32_t v = *fbp;

    if (permedia2->linear_byte_control[1] == 2) {
        v = do_byteswap_32(v);
        v = (v >> 16) | (v << 16);
    } else if (permedia2->linear_byte_control[1] == 1) {
        v = do_byteswap_32(v);
    }

    return v;
}

static void permedia2_write_linear1(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    addr &= svga->vram_mask;
    uint8_t *fbp = (uint8_t*)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}
static void permedia2_writew_linear1(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    if (permedia2->linear_byte_control[0] == 2) {
        val = (val >> 8) | (val << 8);
    }

    addr &= svga->vram_mask;
    uint16_t *fbp = (uint16_t *)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}
static void permedia2_writel_linear1(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t*)p;
    permedia2_t *permedia2 = (permedia2_t*)svga->p;

    if (permedia2->linear_byte_control[0] == 2) {
        val = (val >> 16) | (val << 16);
        val = do_byteswap_32(val);
    } else if (permedia2->linear_byte_control[0] == 1) {
        val = do_byteswap_32(val);
    }

    addr &= svga->vram_mask;
    uint32_t *fbp = (uint32_t*)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}

static void permedia2_write_linear2(uint32_t addr, uint8_t val, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    addr &= svga->vram_mask;
    uint8_t *fbp = (uint8_t *)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}
static void permedia2_writew_linear2(uint32_t addr, uint16_t val, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    if (permedia2->linear_byte_control[1] == 2) {
        val = (val >> 8) | (val << 8);
    }

    addr &= svga->vram_mask;
    uint16_t *fbp = (uint16_t *)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}
static void permedia2_writel_linear2(uint32_t addr, uint32_t val, void *p)
{
    svga_t *svga = (svga_t *)p;
    permedia2_t *permedia2 = (permedia2_t *)svga->p;

    if (permedia2->linear_byte_control[1] == 2) {
        val = (val >> 16) | (val << 16);
        val = do_byteswap_32(val);
    } else if (permedia2->linear_byte_control[1] == 1) {
        val = do_byteswap_32(val);
    }

    addr &= svga->vram_mask;
    uint32_t *fbp = (uint32_t *)(&svga->vram[addr]);
    *fbp = val;
    svga->changedvram[addr >> 12] = changeframecount;
}

void permedia2_updatebanking(permedia2_t *permedia2)
{
    svga_t *svga = &permedia2->svga;

    svga->banked_mask = 0xffff;
}

void permedia2_updatemapping(permedia2_t *permedia2)
{
        svga_t *svga = &permedia2->svga;
        
        permedia2_updatebanking(permedia2);

        mem_mapping_disablex(&svga->mapping);
        mem_mapping_disablex(&permedia2->mmio_mapping);
        mem_mapping_disablex(&permedia2->linear_mapping[0]);
        mem_mapping_disablex(&permedia2->linear_mapping[1]);

        if (permedia2->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM) {
            if (permedia2->linear_base[0]) {
                mem_mapping_set_addrx(&permedia2->linear_mapping[0], permedia2->linear_base[0], 0x00800000);
            }
            if (permedia2->linear_base[1]) {
                mem_mapping_set_addrx(&permedia2->linear_mapping[1], permedia2->linear_base[1], 0x00800000);
            }
            if (permedia2->mmio_base) {
                mem_mapping_set_addrx(&permedia2->mmio_mapping, permedia2->mmio_base, 0x20000);
            }
        }
}

static void permedia2_adjust_panning(svga_t *svga)
{
    int ar11 = svga->attrregs[0x13] & 7;
    int src = 0, dst = 8;

    switch (svga->bpp)
    {
        case 15:
        case 16:
            dst = (ar11 & 4) ? 7 : 8;
            break;
        case 24:
            src = ar11 >> 1;
            break;
        default:
            return;
    }

    dst += 24;
    svga->scrollcache_dst = dst;
    svga->scrollcache_src = src;

}

static inline uint32_t dword_remap(uint32_t in_addr)
{
    return in_addr;
}

static void permedia2_io_remove(permedia2_t *permedia2)
{
    io_removehandlerx(0x03c0, 0x0020, permedia2_in, NULL, NULL, permedia2_out, NULL, NULL, permedia2);
}
static void permedia2_io_set(permedia2_t *permedia2)
{
    permedia2_io_remove(permedia2);

    io_sethandlerx(0x03c0, 0x0020, permedia2_in, NULL, NULL, permedia2_out, NULL, NULL, permedia2);
}

uint8_t permedia2_pci_read(int func, int addr, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;
    switch (addr)
    {
        case 0x00: return 0x4c; /* 3DLabs */
        case 0x01: return 0x10;

        case 0x02: return permedia2->id_ext_pci;
        case 0x03: return 0x3d;

        case PCI_REG_COMMAND:
            return permedia2->pci_regs[PCI_REG_COMMAND]; /*Respond to IO and memory accesses*/

        case 0x07: return 1 << 1; /*Medium DEVSEL timing*/

        case 0x08: return 1; /*Revision ID*/
        case 0x09: return 0; /*Programming interface*/
        case 0x0a: return 0x80; /*Supports VGA interface*/
        case 0x0b: return 0x03;

        // Control space (MMIO)
        case 0x10: return 0x00;
        case 0x11: return 0x00;
        case 0x12: return (permedia2->mmio_base >> 16) & 0xfe;
        case 0x13: return permedia2->mmio_base >> 24;
        // Aperture 1
        case 0x14: return 0x00;
        case 0x15: return 0x00;
        case 0x16: return (permedia2->linear_base[0] >> 16) & 0x80;
        case 0x17: return permedia2->linear_base[0] >> 24;
        // Aperture 2
        case 0x18: return 0x00;
        case 0x19: return 0x00;
        case 0x1a: return (permedia2->linear_base[1] >> 16) & 0x80;
        case 0x1b: return permedia2->linear_base[1] >> 24;

        case 0x2c: return 0x4c;
        case 0x2d: return 0x10;
        case 0x2e: return permedia2->id_ext_pci;;
        case 0x2f: return 0x3d;

        case 0x30: return permedia2->pci_regs[0x30] & 0x01; /*BIOS ROM address*/
        case 0x31: return 0x00;
        case 0x32: return permedia2->pci_regs[0x32];
        case 0x33: return permedia2->pci_regs[0x33];

        case 0x3c: return permedia2->int_line;
        case 0x3d: return PCI_INTA;
    }
    return 0;
}

void permedia2_pci_write(int func, int addr, uint8_t val, void *p)
{
    permedia2_t *permedia2 = (permedia2_t *)p;
    svga_t *svga = &permedia2->svga;
    switch (addr)
    {
        case PCI_REG_COMMAND:
            permedia2->pci_regs[PCI_REG_COMMAND] = val & 0x23;
            if (val & PCI_COMMAND_IO)
                permedia2_io_set(permedia2);
            else
                permedia2_io_remove(permedia2);
            permedia2_updatemapping(permedia2);
            break;

        case 0x12:
            permedia2->mmio_base &= 0xff00ffff;
            permedia2->mmio_base |= (val & 0xfe) << 16;
            permedia2_updatemapping(permedia2);
            break;
        case 0x13:
            permedia2->mmio_base &= 0x00ffffff;
            permedia2->mmio_base |= val << 24;
            permedia2_updatemapping(permedia2);
            break;

        case 0x16:
            permedia2->linear_base[0] &= 0xff00ffff;
            permedia2->linear_base[0] |= (val & 0x80) << 16;
            permedia2_updatemapping(permedia2);
            break;
        case 0x17:
            permedia2->linear_base[0] &= 0x00ffffff;
            permedia2->linear_base[0] |= val << 24;
            permedia2_updatemapping(permedia2);
            break;

        case 0x1a:
            permedia2->linear_base[1] &= 0xff00ffff;
            permedia2->linear_base[1] |= (val & 0x80) << 16;
            permedia2_updatemapping(permedia2);
            break;
        case 0x1b:
            permedia2->linear_base[1] &= 0x00ffffff;
            permedia2->linear_base[1] |= val << 24;
            permedia2_updatemapping(permedia2);
            break;

        case 0x30: case 0x32: case 0x33:
            permedia2->pci_regs[addr] = val;
            if (permedia2->pci_regs[0x30] & 0x01)
            {
                uint32_t addr = (permedia2->pci_regs[0x32] << 16) | (permedia2->pci_regs[0x33] << 24);
                mem_mapping_set_addrx(&permedia2->bios_rom.mapping, addr, 0x8000);
            } else {
                mem_mapping_disablex(&permedia2->bios_rom.mapping);
            }
            return;

        case 0x3c:
            permedia2->int_line = val;
            return;
    }
}


static void *permedia2_init(char *bios_fn, int chip)
{
        permedia2_t *permedia2 = (permedia2_t*)calloc(sizeof(permedia2_t), 1);
        svga_t *svga = &permedia2->svga;
        int vram;
        uint32_t vram_size;
        
        memset(permedia2, 0, sizeof(permedia2_t));
        
        vram = device_get_config_int("memory");
        if (vram)
                vram_size = vram << 20;
        else
                vram_size = 512 << 10;
        permedia2->vram_mask = vram_size - 1;

        rom_init(&permedia2->bios_rom, bios_fn, 0xc0000, 0x8000, 0x7fff, 0, MEM_MAPPING_EXTERNAL);

        svga_init(&permedia2->svga, permedia2, vram_size,
            permedia2_recalctimings,
            permedia2_in, permedia2_out,
            permedia2_hwcursor_draw,
            NULL);

        mem_mapping_addx(&permedia2->linear_mapping[0], 0, 0, permedia2_read_linear1, permedia2_readw_linear1, permedia2_readl_linear1, permedia2_write_linear1, permedia2_writew_linear1, permedia2_writel_linear1, NULL, MEM_MAPPING_EXTERNAL, &permedia2->svga);
        mem_mapping_addx(&permedia2->linear_mapping[1], 0, 0, permedia2_read_linear2, permedia2_readw_linear2, permedia2_readl_linear2, permedia2_write_linear2, permedia2_writew_linear2, permedia2_writel_linear2, NULL, MEM_MAPPING_EXTERNAL, &permedia2->svga);
        mem_mapping_set_handlerx(&permedia2->svga.mapping, permedia2_read, permedia2_readw, permedia2_readl, permedia2_write, permedia2_writew, permedia2_writel);
        mem_mapping_addx(&permedia2->mmio_mapping, 0, 0, permedia2_mmio_read, permedia2_mmio_readw, permedia2_mmio_readl, permedia2_mmio_write, permedia2_mmio_write_w, permedia2_mmio_write_l, NULL, MEM_MAPPING_EXTERNAL, permedia2);
        mem_mapping_set_px(&permedia2->svga.mapping, permedia2);
        mem_mapping_disablex(&permedia2->mmio_mapping);

        svga->vram_max = 8 << 20;
        svga->decode_mask = svga->vram_max - 1;
        svga->vram_mask = svga->vram_max - 1;

        permedia2->pci_regs[4] = 3;
        permedia2->pci_regs[5] = 0;
        permedia2->pci_regs[6] = 0;
        permedia2->pci_regs[7] = 2;
        permedia2->pci_regs[0x32] = 0x0c;
        permedia2->pci_regs[0x3d] = 1;
        permedia2->pci_regs[0x3e] = 4;
        permedia2->pci_regs[0x3f] = 0xff;


        svga->vsync_callback = permedia2_vblank_start;
        svga->adjust_panning = permedia2_adjust_panning;

        permedia2->chip = chip;

        permedia2_updatemapping(permedia2);

        permedia2->card = pci_add(permedia2_pci_read, permedia2_pci_write, permedia2);

        return permedia2;
}

void *permedia2_init()
{
    permedia2_t *permedia2 = (permedia2_t *)permedia2_init("permedia2.bin", 0);

    permedia2->svga.fb_only = -1;

    permedia2->getclock = sdac_getclock;
    permedia2->getclock_p = &permedia2->ramdac;
    permedia2->id_ext_pci = 0x07;
    sdac_init(&permedia2->ramdac);
    svga_set_ramdac_type(&permedia2->svga, RAMDAC_8BIT);
    svga_recalctimings(&permedia2->svga);

    return permedia2;
}

void permedia2_close(void *p)
{
        permedia2_t *permedia2 = (permedia2_t*)p;

        svga_close(&permedia2->svga);
        
        free(permedia2);
}

void permedia2_speed_changed(void *p)
{
        permedia2_t *permedia2 = (permedia2_t *)p;
        
        svga_recalctimings(&permedia2->svga);
}

void permedia2_force_redraw(void *p)
{
        permedia2_t *permedia2 = (permedia2_t *)p;

        permedia2->svga.fullchange = changeframecount;
}

void permedia2_add_status_info(char *s, int max_len, void *p)
{
        permedia2_t *permedia2 = (permedia2_t *)p;
        uint64_t new_time = timer_read();
    
        svga_add_status_info(s, max_len, &permedia2->svga);
}

device_t permedia2_device =
{
    "Permedia 2",
    0,
    permedia2_init,
    permedia2_close,
    NULL,
    permedia2_speed_changed,
    permedia2_force_redraw,
    permedia2_add_status_info,
    NULL
};
