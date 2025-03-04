#include "ibm.h"
#include "device.h"
#include "mem.h"
#include "thread.h"
#include "video.h"
#include "vid_svga.h"
#include "vid_voodoo.h"
#include "vid_voodoo_common.h"
#include "vid_voodoo_dither.h"
#include "vid_voodoo_regs.h"
#include "vid_voodoo_render.h"
#include "vid_voodoo_fb.h"

uint16_t voodoo_fb_readw(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        int x, y;
        uint32_t read_addr;
        uint16_t temp;

        if (voodoo->type >= VOODOO_BANSHEE)
        {
                x = addr & 0xffe;
                y = (addr >> 12) & 0x3ff;
        }
        else
        {
                x = addr & 0x7fe;
                y = (addr >> 11) & 0x3ff;
        }

        if (SLI_ENABLED)
        {
                voodoo_set_t *set = voodoo->set;

                if (y & 1)
                        voodoo = set->voodoos[1];
                else
                        voodoo = set->voodoos[0];

                y >>= 1;
        }

        if (voodoo->col_tiled)
                read_addr = voodoo->fb_read_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffff;

        temp = *(uint16_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        pclog("voodoo_fb_readw : %08X %08X  %i %i  %08X %08X  %08x:%08x %i\n", addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++);
        return temp;
}
uint32_t voodoo_fb_readl(uint32_t addr, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        int x, y;
        uint32_t read_addr;
        uint32_t temp;

        if (voodoo->type >= VOODOO_BANSHEE)
        {
                x = addr & 0xffe;
                y = (addr >> 12) & 0x3ff;
        }
        else
        {
                x = addr & 0x7fe;
                y = (addr >> 11) & 0x3ff;
        }

        if (SLI_ENABLED)
        {
                voodoo_set_t *set = voodoo->set;

                if (y & 1)
                        voodoo = set->voodoos[1];
                else
                        voodoo = set->voodoos[0];

                y >>= 1;
        }

        if (voodoo->col_tiled)
                read_addr = voodoo->fb_read_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                read_addr = voodoo->fb_read_offset + x + (y * voodoo->row_width);

        if (read_addr > voodoo->fb_mask)
                return 0xffffffff;

        temp = *(uint32_t *)(&voodoo->fb_mem[read_addr & voodoo->fb_mask]);

//        pclog("voodoo_fb_readl : %08X %08x %08X  x=%i y=%i  %08X %08X  %08x:%08x %i ro=%08x rw=%i\n", addr, read_addr, temp, x, y, read_addr, *(uint32_t *)(&voodoo->fb_mem[4]), cs, pc, fb_reads++, voodoo->fb_read_offset, voodoo->row_width);
        return temp;
}

static inline uint16_t do_dither(voodoo_params_t *params, rgba8_t col, int x, int y)
{
        int r, g, b;

        if (dither)
        {
                if (dither2x2)
                {
                        r = dither_rb2x2[col.r][y & 1][x & 1];
                        g =  dither_g2x2[col.g][y & 1][x & 1];
                        b = dither_rb2x2[col.b][y & 1][x & 1];
                }
                else
                {
                        r = dither_rb[col.r][y & 3][x & 3];
                        g =  dither_g[col.g][y & 3][x & 3];
                        b = dither_rb[col.b][y & 3][x & 3];
                }
        }
        else
        {
                r = col.r >> 3;
                g = col.g >> 2;
                b = col.b >> 3;
        }

        return b | (g << 5) | (r << 11);
}

void voodoo_fb_writew(uint32_t addr, uint16_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        voodoo_params_t *params = &voodoo->params;
        int x, y;
        uint32_t write_addr, write_addr_aux;
        rgba8_t colour_data;
        uint16_t depth_data;
        uint8_t alpha_data;
        int write_mask = 0;

        colour_data.r = colour_data.g = colour_data.b = colour_data.a = 0;

        depth_data = voodoo->params.zaColor & 0xffff;
        alpha_data = voodoo->params.zaColor >> 24;

//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);

//        pclog("voodoo_fb_writew : %08X %04X\n", addr, val);


        switch (voodoo->lfbMode & LFB_FORMAT_MASK)
        {
                case LFB_FORMAT_RGB565:
                colour_data = rgb565[val];
                alpha_data = 0xff;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_RGB555:
                colour_data = argb1555[val];
                alpha_data = 0xff;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_ARGB1555:
                colour_data = argb1555[val];
                alpha_data = colour_data.a;
                write_mask = LFB_WRITE_COLOUR;
                break;
                case LFB_FORMAT_DEPTH:
                depth_data = val;
                write_mask = LFB_WRITE_DEPTH;
                break;

                default:
                fatal("voodoo_fb_writew : bad LFB format %08X\n", voodoo->lfbMode);
        }

        if (voodoo->type >= VOODOO_BANSHEE)
        {
                x = addr & 0xffe;
                y = (addr >> 12) & 0x3ff;
        }
        else
        {
                x = addr & 0x7fe;
                y = (addr >> 11) & 0x3ff;
        }

        if (SLI_ENABLED)
        {
                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
                        return;
                y >>= 1;
        }


        if (voodoo->fb_write_offset == voodoo->params.front_offset && y < 2048)
                voodoo->dirty_line[y] = 1;

        if (voodoo->col_tiled)
                write_addr = voodoo->fb_write_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        if (voodoo->aux_tiled)
                write_addr_aux = voodoo->params.aux_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);

//        pclog("fb_writew %08x %i %i %i %08x\n", addr, x, y, voodoo->row_width, write_addr);

        if (voodoo->lfbMode & 0x100)
        {
                {
                        rgba8_t write_data = colour_data;
                        uint16_t new_depth = depth_data;

                        if (params->fbzMode & FBZ_DEPTH_ENABLE)
                        {
                                uint16_t old_depth = *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                                DEPTH_TEST(new_depth);
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int64_t w_depth = (int64_t)(int32_t)new_depth;
                                int32_t ia = alpha_data << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
                        }

                        if (params->alphaMode & 1)
                                ALPHA_TEST(alpha_data);

                        if (params->alphaMode & (1 << 4))
                        {
                                uint16_t dat = *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                                int dest_r, dest_g, dest_b, dest_a;

                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;

                                ALPHA_BLEND(write_data.r, write_data.g, write_data.b, alpha_data);
                        }

                        if (params->fbzMode & FBZ_RGB_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, x >> 1, y);
                        if (params->fbzMode & FBZ_DEPTH_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
                        return;
                }
        }
        else
        {
                if (write_mask & LFB_WRITE_COLOUR)
                        *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, colour_data, x >> 1, y);
                if (write_mask & LFB_WRITE_DEPTH)
                        *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data;
        }
}


void voodoo_fb_writel(uint32_t addr, uint32_t val, void *p)
{
        voodoo_t *voodoo = (voodoo_t *)p;
        voodoo_params_t *params = &voodoo->params;
        int x, y;
        uint32_t write_addr, write_addr_aux;
        rgba8_t colour_data[2];
        uint16_t depth_data[2];
        uint8_t alpha_data[2];
        int write_mask = 0, count = 1;

        depth_data[0] = depth_data[1] = voodoo->params.zaColor & 0xffff;
        alpha_data[0] = alpha_data[1] = voodoo->params.zaColor >> 24;
//        while (!RB_EMPTY)
//                thread_reset_event(voodoo->not_full_event);

//        pclog("voodoo_fb_writel : %08X %08X\n", addr, val);

        switch (voodoo->lfbMode & LFB_FORMAT_MASK)
        {
                case LFB_FORMAT_RGB565:
                colour_data[0] = rgb565[val & 0xffff];
                colour_data[1] = rgb565[val >> 16];
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;
                case LFB_FORMAT_RGB555:
                colour_data[0] = argb1555[val & 0xffff];
                colour_data[1] = argb1555[val >> 16];
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;
                case LFB_FORMAT_ARGB1555:
                colour_data[0] = argb1555[val & 0xffff];
                alpha_data[0] = colour_data[0].a;
                colour_data[1] = argb1555[val >> 16];
                alpha_data[1] = colour_data[1].a;
                write_mask = LFB_WRITE_COLOUR;
                count = 2;
                break;

                case LFB_FORMAT_ARGB8888:
                colour_data[0].b = val & 0xff;
                colour_data[0].g = (val >> 8) & 0xff;
                colour_data[0].r = (val >> 16) & 0xff;
                alpha_data[0] = (val >> 24) & 0xff;
                write_mask = LFB_WRITE_COLOUR;
                addr >>= 1;
                break;

                case LFB_FORMAT_XRGB8888:
                colour_data[0].b = val & 0xff;
                colour_data[0].g = (val >> 8) & 0xff;
                colour_data[0].r = (val >> 16) & 0xff;
                alpha_data[0] = 0xff;
                write_mask = LFB_WRITE_COLOUR;
                addr >>= 1;
                break;

                case LFB_FORMAT_DEPTH:
                depth_data[0] = val;
                depth_data[1] = val >> 16;
                write_mask = LFB_WRITE_DEPTH;
                count = 2;
                break;

                default:
                fatal("voodoo_fb_writel : bad LFB format %08X\n", voodoo->lfbMode);
        }

        if (voodoo->type >= VOODOO_BANSHEE)
        {
                x = addr & 0xffe;
                y = (addr >> 12) & 0x3ff;
        }
        else
        {
                x = addr & 0x7fe;
                y = (addr >> 11) & 0x3ff;
        }

        if (SLI_ENABLED)
        {
                if ((!(voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && (y & 1)) ||
                    ((voodoo->initEnable & INITENABLE_SLI_MASTER_SLAVE) && !(y & 1)))
                        return;
                y >>= 1;
        }

        if (voodoo->fb_write_offset == voodoo->params.front_offset && y < 2048)
                voodoo->dirty_line[y] = 1;

        if (voodoo->col_tiled)
                write_addr = voodoo->fb_write_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                write_addr = voodoo->fb_write_offset + x + (y * voodoo->row_width);
        if (voodoo->aux_tiled)
                write_addr_aux = voodoo->params.aux_offset + (x & 127) + (x >> 7) * 128*32 + (y & 31) * 128 + (y >> 5) * voodoo->row_width;
        else
                write_addr_aux = voodoo->params.aux_offset + x + (y * voodoo->row_width);

//        pclog("fb_writel %08x x=%i y=%i rw=%i %08x wo=%08x\n", addr, x, y, voodoo->row_width, write_addr, voodoo->fb_write_offset);

        if (voodoo->lfbMode & 0x100)
        {
                int c;

                for (c = 0; c < count; c++)
                {
                        rgba8_t write_data = colour_data[c];
                        uint16_t new_depth = depth_data[c];

                        if (params->fbzMode & FBZ_DEPTH_ENABLE)
                        {
                                uint16_t old_depth = *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]);

                                DEPTH_TEST(new_depth);
                        }

                        if ((params->fbzMode & FBZ_CHROMAKEY) &&
                                write_data.r == params->chromaKey_r &&
                                write_data.g == params->chromaKey_g &&
                                write_data.b == params->chromaKey_b)
                                goto skip_pixel;

                        if (params->fogMode & FOG_ENABLE)
                        {
                                int32_t z = new_depth << 12;
                                int64_t w_depth = new_depth;
                                int32_t ia = alpha_data[c] << 12;

                                APPLY_FOG(write_data.r, write_data.g, write_data.b, z, ia, w_depth);
                        }

                        if (params->alphaMode & 1)
                                ALPHA_TEST(alpha_data[c]);

                        if (params->alphaMode & (1 << 4))
                        {
                                uint16_t dat = *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]);
                                int dest_r, dest_g, dest_b, dest_a;

                                dest_r = (dat >> 8) & 0xf8;
                                dest_g = (dat >> 3) & 0xfc;
                                dest_b = (dat << 3) & 0xf8;
                                dest_r |= (dest_r >> 5);
                                dest_g |= (dest_g >> 6);
                                dest_b |= (dest_b >> 5);
                                dest_a = 0xff;

                                ALPHA_BLEND(write_data.r, write_data.g, write_data.b, alpha_data[c]);
                        }

                        if (params->fbzMode & FBZ_RGB_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, write_data, (x >> 1) + c, y);
                        if (params->fbzMode & FBZ_DEPTH_WMASK)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = new_depth;

skip_pixel:
                        write_addr += 2;
                        write_addr_aux += 2;
                }
        }
        else
        {
                int c;

                for (c = 0; c < count; c++)
                {
                        if (write_mask & LFB_WRITE_COLOUR)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr & voodoo->fb_mask]) = do_dither(&voodoo->params, colour_data[c], (x >> 1) + c, y);
                        if (write_mask & LFB_WRITE_DEPTH)
                                *(uint16_t *)(&voodoo->fb_mem[write_addr_aux & voodoo->fb_mask]) = depth_data[c];

                        write_addr += 2;
                        write_addr_aux += 2;
                }
        }
}
