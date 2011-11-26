#include "sysconfig.h"
#include "sysdeps.h"

#include <ctype.h>
#include <assert.h>

#include "options.h"
#include "xwin.h"
#include "custom.h"

static bool automatic;
static int monitor;

extern unsigned int bplcon0;

static uae_u8 graffiti_palette[256 * 4];

static bool graffiti(struct vidbuffer *src, struct vidbuffer *dst)
{
	int y, x;
	int ystart, yend, isntsc;
	int xstart, xend;
	uae_u8 *srcbuf, *srcend;
	uae_u8 *dstbuf;
	bool command, hires, found;
	int xadd, xpixadd, extrapix;
	int waitline = 0, dbl;
	uae_u8 read_mask = 0xff, color = 0, color2 = 0;

	if (!(bplcon0 & 0x0100)) // GAUD
		return false;

	command = true;
	found = false;
	isntsc = (beamcon0 & 0x20) ? 0 : 1;
	if (!(currprefs.chipset_mask & CSMASK_ECS_AGNUS))
		isntsc = currprefs.ntscmode ? 1 : 0;

	dbl = gfxvidinfo.ychange == 1 ? 2 : 1;

	ystart = isntsc ? VBLANK_ENDLINE_NTSC : VBLANK_ENDLINE_PAL;
	yend = isntsc ? MAXVPOS_NTSC : MAXVPOS_PAL;
	if (src->yoffset >= (ystart << VRES_MAX))
		ystart = src->yoffset >> VRES_MAX;

	xadd = gfxvidinfo.xchange == 1 ? src->pixbytes * 2 : src->pixbytes;
	xpixadd = gfxvidinfo.xchange == 1 ? 4 : 2;

	xstart = 0x1c * 2 + 1;
	xend = 0xf0 * 2 + 1;

	srcbuf = src->bufmem + (((ystart << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((xstart << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;
	srcend = src->bufmem + (((yend << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes;
	extrapix = 0;

	dstbuf = dst->bufmem + (((ystart << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * dst->rowbytes + (((xstart << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * dst->pixbytes;

	y = 0;
	while (srcend > srcbuf) {
		uae_u8 *srcp = srcbuf + extrapix;
		uae_u8 *dstp = dstbuf;

		x = xstart;
		while (x < xend) {
			
			uae_u8 mask = 0x80;
			uae_u8 chunky[4] = { 0, 0, 0, 0 };
			while (mask) {
				if (srcp[2] & 0x80) // R
					chunky[3] |= mask;
				if (srcp[1] & 0x80) // G
					chunky[2] |= mask;
				if (srcp[0] & 0x80) // B
					chunky[1] |= mask;
				if (srcp[0] & 0x10) // I
					chunky[0] |= mask;
				srcp += xadd;
				mask >>= 1;
			}

			if (command) {
				if (chunky[0] || chunky[1] || chunky[2] || chunky[3] || found) {
					for (int pix = 0; pix < 2; pix++) {
						uae_u8 cmd = chunky[pix * 2 + 0];
						uae_u8 parm = chunky[pix * 2 + 1];

#if 0
						//if (cmd != 0)
							write_log(L"X=%d Y=%d %02x = %02x (%d %d)\n", x, y, cmd, parm, color, color2);
#endif

						if (automatic && cmd >= 0x40)
							return false;
						if (cmd != 0)
							found = true;
						if (cmd & 8) {
							command = false;
							dbl = 1;
							waitline = 2;
							if (cmd & 16) {
								hires = true;
								xadd /= 2;
								xpixadd /= 2;
								extrapix = -4 * src->pixbytes;
							} else {
								hires = false;
							}
						} else if (cmd & 4) {
							if ((cmd & 3) == 1) {
								read_mask = parm;
							} else if ((cmd & 3) == 2) {
								graffiti_palette[color * 4 + color2] = (parm << 2) | (parm & 3);
								color2++;
								if (color2 == 3) {
									color2 = 0;
									color++;
								}
							} else if ((cmd & 3) == 0) {
								color = parm;
								color2 = 0;
							}
						}
					}
				}

				memset(dstp, 0, dst->pixbytes * 4 * 2);
				dstp += dst->pixbytes * 4 * 2;

			} else if (waitline) {
			
				memset(dstp, 0, dst->pixbytes * 4 * 2);
				dstp += dst->pixbytes * 4 * 2;
			
			} else {

				for (int pix = 0; pix < 4; pix++) {
					uae_u8 r, g, b, c;
					
					c = chunky[pix] & read_mask;
					r = graffiti_palette[c * 4 + 0];
					g = graffiti_palette[c * 4 + 1];
					b = graffiti_palette[c * 4 + 2];
					dstp[2] = r;
					dstp[1] = g;
					dstp[0] = b;
					dstp += dst->pixbytes;
					dstp[2] = r;
					dstp[1] = g;
					dstp[0] = b;
					dstp += dst->pixbytes;
					
					if (gfxvidinfo.xchange == 1 && !hires) {
						dstp[2] = r;
						dstp[1] = g;
						dstp[0] = b;
						dstp += dst->pixbytes;
						dstp[2] = r;
						dstp[1] = g;
						dstp[0] = b;
						dstp += dst->pixbytes;
					}
				}

			}

			x += xpixadd;
		}

		y++;
		srcbuf += src->rowbytes * dbl;
		dstbuf += dst->rowbytes * dbl;
		if (waitline > 0)
			waitline--;
	}

	dst->nativepositioning = true;

	if (monitor != MONITOREMU_GRAFFITI) {
		monitor = MONITOREMU_GRAFFITI;
		write_log (L"GRAFFITI %s mode\n", hires ? L"hires" : L"lores");
	}

	return true;
}

/* A2024 information comes from US patent 4851826 */

static bool a2024(struct vidbuffer *src, struct vidbuffer *dst)
{
	int y;
	uae_u8 *srcbuf, *dstbuf;
	uae_u8 *dataline;
	int px, py, doff, pxcnt, dbl;
	int panel_width, panel_width_draw, panel_height, srcxoffset;
	bool f64, interlace, expand, wpb, less16;
	uae_u8 enp, dpl;
	bool hires;
	int idline;
	int total_width, total_height;
	
	idline = currprefs.ntscmode ? 21 : 29;

	if (src->yoffset > (idline << VRES_MAX))
		return false;

	dbl = gfxvidinfo.ychange == 1 ? 2 : 1;
	doff = (128 * 2 / gfxvidinfo.xchange) * src->pixbytes;
	// min 178 max 234
	dataline = src->bufmem + (((idline << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((200 << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;

#if 0
	write_log (L"%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x\n",
		dataline[0 * doff + 0], dataline[0 * doff + 1], dataline[0 * doff + 2],
		dataline[1 * doff + 0], dataline[1 * doff + 1], dataline[1 * doff + 2],
		dataline[2 * doff + 0], dataline[2 * doff + 1], dataline[2 * doff + 2],
		dataline[3 * doff + 0], dataline[3 * doff + 1], dataline[3 * doff + 2]);
#endif

	px = py = 0;
	if (dataline[1 * doff + 0] & 0x80) // 1:B FN2
		px |= 2;
	if (dataline[1 * doff + 1] & 0x80) // 1:G FN1
		px |= 1;
	if (dataline[1 * doff + 2] & 0x80) // 1:R FN0
		py |= 1;

	f64 = (dataline[0 * doff + 2] & 0x80) != 0;			// 0:R
	interlace = (dataline[0 * doff + 1] & 0x80) != 0;	// 0:G (*Always zero)
	if ((dataline[0 * doff + 0] & 0x80) != 0)			// 0:B = 0
		return false;
	if ((dataline[0 * doff + 0] & 0x10) == 0)			// 0:I = 1
		return false;
	expand = (dataline[1 * doff + 0] & 0x10) != 0;		// 1:I (*Always set)
	enp = (dataline[2 * doff + 2] & 0x80) ? 1 : 0;		// 2:R (*ENP=3)
	enp |= (dataline[2 * doff + 1] & 0x80) ? 2 : 0;		// 2:G
	wpb = (dataline[2 * doff + 0] & 0x80) != 0;			// 2:B (*Always zero)
	if ((dataline[2 * doff + 0] & 0x10) != 0)			// 2:I = 0
		return false;
	dpl = (dataline[3 * doff + 2] & 0x80) ? 1 : 0;		// 3:R (*DPL=3)
	dpl |= (dataline[3 * doff + 1] & 0x80) ? 2 : 0;		// 3:G
	less16 = (dataline[3 * doff + 0] & 0x80) != 0;		// 3:B
	if ((dataline[3 * doff + 0] & 0x10) == 0)			// 3:I = 1
		return false;

	/* (*) = AOS A2024 driver static bits. Not yet implemented in emulation. */

	if (f64) {
		panel_width = 336;
		panel_width_draw = 352;
		pxcnt = 3;
		hires = false;
		srcxoffset = 113;
		if (px > 2)
			return false;
	} else {
		panel_width = 512;
		panel_width_draw = 512;
		pxcnt = 2;
		hires = true;
		srcxoffset = 129;
		if (px > 1)
			return false;
	}
	panel_height = currprefs.ntscmode ? 400 : 512;


#if 0
	write_log (L"0 = F6-4:%d INTERLACE:%d\n", f64, interlace);
	write_log (L"1 = FN:%d EXPAND:%d\n", py + px *2, expand);
	write_log (L"2 = ENP:%d WPB=%d\n", enp, wpb);
	write_log (L"3 = DPL:%d LESS16=%d\n", dpl, less16);
#endif
#if 0
	write_log (L"%02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %dx%d\n",
		dataline[0 * doff + 0], dataline[0 * doff + 1], dataline[0 * doff + 2],
		dataline[1 * doff + 0], dataline[1 * doff + 1], dataline[1 * doff + 2],
		dataline[2 * doff + 0], dataline[2 * doff + 1], dataline[2 * doff + 2],
		dataline[3 * doff + 0], dataline[3 * doff + 1], dataline[3 * doff + 2],
		px, py);
#endif

	total_width = panel_width * (pxcnt - 1) + panel_width_draw;
	if (total_width > 1024)
		total_width = 1024;
	if (less16) {
		total_width -= 16;
		if (px == pxcnt - 1)
			panel_width_draw -= 16;
	}
	total_height = panel_height * dbl;
	
	srcbuf = src->bufmem + (((44 << VRES_MAX) - src->yoffset) / gfxvidinfo.ychange) * src->rowbytes + (((srcxoffset << RES_MAX) - src->xoffset) / gfxvidinfo.xchange) * src->pixbytes;
	dstbuf = dst->bufmem + py * (panel_height / gfxvidinfo.ychange) * dst->rowbytes + px * ((panel_width * 2) / gfxvidinfo.xchange) * dst->pixbytes;

	for (y = 0; y < (panel_height / (dbl == 1 ? 1 : 2)) / gfxvidinfo.ychange; y++) {
#if 0
		memcpy (dstbuf, srcbuf, ((panel_width * 2) / gfxvidinfo.xchange) * dst->pixbytes);
#else
		uae_u8 *srcp = srcbuf;
		uae_u8 *dstp1 = dstbuf;
		uae_u8 *dstp2 = dstbuf + dst->rowbytes;
		int x;
		for (x = 0; x < (panel_width_draw * 2) / gfxvidinfo.xchange; x++) {
			uae_u8 c1 = 0, c2 = 0;
			if (srcp[2] & 0x80) // R
				c1 |= 2;
			if (srcp[1] & 0x80) // G
				c2 |= 2;
			if (srcp[0] & 0x80) // B
				c1 |= 1;
			if (srcp[0] & 0x10) // I
				c2 |= 1;
			if (dbl == 1) {
				c1 = (c1 + c2 + 1) / 2;
				c1 = (c1 << 6) | (c1 << 4) | (c1 << 2);
				dstp1[0] = c1;
				dstp1[1] = c1;
				dstp1[2] = c1;
			} else {
				c1 = (c1 << 6) | (c1 << 4) | (c1 << 2);
				c2 = (c2 << 6) | (c2 << 4) | (c2 << 2);
				dstp1[0] = c1;
				dstp1[1] = c1;
				dstp1[2] = c1;
				dstp2[0] = c2;
				dstp2[1] = c2;
				dstp2[2] = c2;
				dstp2 += dst->pixbytes;
			}
			srcp += src->pixbytes;
			if (!hires)
				srcp += src->pixbytes;
			dstp1 += dst->pixbytes;
		}
#endif
		srcbuf += src->rowbytes * dbl;
		dstbuf += dst->rowbytes * dbl;
	}

	total_width /= 2;
	total_width <<= currprefs.gfx_resolution;

	dst->outwidth = total_width;
	dst->outheight = total_height;
	dst->inwidth = total_width;
	dst->inheight = total_height;
	dst->inwidth2 = total_width;
	dst->inheight2 = total_height;
	dst->nativepositioning = false;

	if (monitor != MONITOREMU_A2024) {
		monitor = MONITOREMU_A2024;
		write_log (L"A2024 %dHz mode\n", hires ? 10 : 15);
	}

	return true;
}

static bool emulate_specialmonitors2(struct vidbuffer *src, struct vidbuffer *dst)
{
	if (src->pixbytes != 4)
		return false;

	if (currprefs.monitoremu == MONITOREMU_AUTO) {
		automatic = true;
		bool v = a2024(src, dst);
		if (!v)
			v = graffiti(src, dst);
		return v;
	} else if (currprefs.monitoremu == MONITOREMU_A2024) {
		automatic = false;
		return a2024(src, dst);
	} else if (currprefs.monitoremu == MONITOREMU_GRAFFITI) {
		automatic = false;
		return graffiti(src, dst);
	}
	return false;
}


bool emulate_specialmonitors(struct vidbuffer *src, struct vidbuffer *dst)
{
	if (!emulate_specialmonitors2(src, dst)) {
		if (monitor) {
			monitor = 0;
			write_log (L"Native mode\n");
		}
		return false;
	}
	return true;
}
