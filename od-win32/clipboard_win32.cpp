
#include "sysconfig.h"
#include "sysdeps.h"

#include <stdlib.h>
#include <stdarg.h>

#include <windows.h>

#include "clipboard_win32.h"
#include "clipboard.h"

#include "threaddep/thread.h"
#include "memory_uae.h"
#include "native2amiga_api.h"

#define DEBUG_CLIP 0

int clipboard_debug;

static HWND chwnd;
static HDC hdc;
static uaecptr clipboard_data;
static int vdelay, signaling, initialized;
static uae_u8 *to_amiga;
static uae_u32 to_amiga_size;
static int clipopen;
static int clipactive;
static int clipboard_change;
static void *clipboard_delayed_data;
static int clipboard_delayed_size;
static bool clip_disabled;

static void debugwrite (const TCHAR *name, uae_u8 *p, int size)
{
	FILE *f;
	int cnt;

	if (!p || !size)
		return;
	cnt = 0;
	for (;;) {
		TCHAR tmp[MAX_DPATH];
		_stprintf (tmp, _T("%s.%03d.dat"), name, cnt);
		f = _tfopen (tmp, _T("rb"));
		if (f) {
			fclose (f);
			cnt++;
			continue;
		}
		f = _tfopen (tmp, _T("wb"));
		if (f) {
			fwrite (p, size, 1, f);
			fclose (f);
		}
		return;
	}
}

static void to_amiga_start (void)
{
	if (!initialized)
		return;
	if (!clipboard_data || get_long (clipboard_data) != 0)
		return;
	if (clipboard_debug) {
		debugwrite (_T("clipboard_p2a"), to_amiga, to_amiga_size);
	}
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: to_amiga %08x %d\n"), clipboard_data, to_amiga_size);
#endif
	put_long (clipboard_data, to_amiga_size);
	uae_Signal (get_long (clipboard_data + 8), 1 << 13);
}

static uae_char *pctoamiga (const uae_char *txt)
{
	int len;
	uae_char *txt2;
	int i, j;

	len = strlen (txt) + 1;
	txt2 = xmalloc (uae_char, len);
	j = 0;
	for (i = 0; i < len; i++) {
		uae_char c = txt[i];
		if (c == 13)
			continue;
		txt2[j++] = c;
	}
	return txt2;
}

static int parsecsi (const char *txt, int off, int len)
{
	while (off < len) {
		if (txt[off] >= 0x40)
			break;
		off++;
	}
	return off;
}

static TCHAR *amigatopc (const char *txt)
{
	int i, j, cnt;
	int len, pc;
	char *txt2;
	TCHAR *s;

	pc = 0;
	cnt = 0;
	len = strlen (txt) + 1;
	for (i = 0; i < len; i++) {
		uae_char c = txt[i];
		if (c == 13)
			pc = 1;
		if (c == 10)
			cnt++;
	}
	if (pc)
		return my_strdup_ansi (txt);
	txt2 = xcalloc (char, len + cnt);
	j = 0;
	for (i = 0; i < len; i++) {
		uae_char c = txt[i];
		if (c == 0 && i + 1 < len)
			continue;
		if (c == 10)
			txt2[j++] = 13;
		if (c == 0x9b) {
			i = parsecsi (txt, i + 1, len);
			continue;
		} else if (c == 0x1b && i + 1 < len && txt[i + 1] == '[') {
			i = parsecsi (txt, i + 2, len);
			continue;
		}
		txt2[j++] = c;
	}
	s = my_strdup_ansi (txt2);
	xfree (txt2);
	return s;
}


static void to_iff_text (const TCHAR *pctxt)
{
	uae_u8 b[] = { 'F','O','R','M',0,0,0,0,'F','T','X','T','C','H','R','S',0,0,0,0 };
	uae_u32 size;
	int txtlen;
	uae_char *txt;
	char *s;

	s = ua (pctxt);
	txt = pctoamiga (s);
	txtlen = strlen (txt);
	xfree (to_amiga);
	size = txtlen + sizeof b + (txtlen & 1) - 8;
	b[4] = size >> 24;
	b[5] = size >> 16;
	b[6] = size >>  8;
	b[7] = size >>  0;
	size = txtlen;
	b[16] = size >> 24;
	b[17] = size >> 16;
	b[18] = size >>  8;
	b[19] = size >>  0;
	to_amiga_size = sizeof b + txtlen + (txtlen & 1);
	to_amiga = xcalloc (uae_u8, to_amiga_size);
	memcpy (to_amiga, b, sizeof b);
	memcpy (to_amiga + sizeof b, txt, txtlen);
	to_amiga_start ();
	xfree (txt);
	xfree (s);
}

static int clipboard_put_text (const TCHAR *txt);
static void from_iff_text (uaecptr ftxt, uae_u32 len)
{
	uae_u8 *addr = NULL, *eaddr;
	char *txt = NULL;
	int txtsize = 0;

#if 0
	{
		FILE *f = fopen("c:\\d\\clipboard_a2p.005.dat", "rb");
		if (f) {
			addr = xmalloc (10000);
			len = fread (addr, 1, 10000, f);
			fclose (f);
		}
	}
#else
	addr = get_real_address (ftxt);
#endif
	eaddr = addr + len;
	if (memcmp ("FTXT", addr + 8, 4))
		return;
	addr += 12;
	while (addr < eaddr) {
		uae_u32 csize = (addr[4] << 24) | (addr[5] << 16) | (addr[6] << 8) | (addr[7] << 0);
		if (addr + 8 + csize > eaddr)
			break;
		if (!memcmp (addr, "CHRS", 4) && csize) {
			int prevsize = txtsize;
			txtsize += csize;
			txt = xrealloc (char, txt, txtsize + 1);
			memcpy (txt + prevsize, addr + 8, csize);
			txt[txtsize] = 0;
		}
		addr += 8 + csize + (csize & 1);
		if (csize >= 1 && addr[-2] == 0x0d && addr[-1] == 0x0a && addr[0] == 0)
			addr++;
		else if (csize >= 1 && addr[-1] == 0x0d && addr[0] == 0x0a)
			addr++;
	}
	if (txt == NULL) {
		clipboard_put_text (_T(""));
	} else {
		TCHAR *pctxt = amigatopc (txt);
		clipboard_put_text (pctxt);
		xfree (pctxt);
	}
	xfree (txt);
}


static void to_iff_ilbm (HBITMAP hbmp)
{
	BITMAP bmp;
	int bmpw, w, h, bpp, iffbpp, tsize, size, x, y, i;
	int iffsize, bodysize;
	uae_u32 colors[256];
	int cnt;
	uae_u8 *iff, *p;
	uae_u8 iffilbm[] = {
		'F','O','R','M',0,0,0,0,'I','L','B','M',
		'B','M','H','D',0,0,0,20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		'C','A','M','G',0,0,0, 4,  0,0,0,0,
	};

	if (!GetObject (hbmp, sizeof bmp, &bmp))
		return;
	w = bmp.bmWidth;
	h = bmp.bmHeight;
	bpp = bmp.bmBitsPixel;

	if (bpp != 8 && bpp != 32)
		return;
	bmpw = (w * bpp / 8 + 3) & ~3;
	size = bmpw * h;
	bmp.bmBits = xmalloc (uae_u8, size);
	if (!GetBitmapBits (hbmp, size, bmp.bmBits)) {
		xfree (bmp.bmBits);
		return;
	}
#if DEBUG_CLIP > 0
	write_log (_T("BMP2IFF: W=%d H=%d bpp=%d\n"), w, h, bpp);
#endif
	iffbpp = bpp > 8 ? 24 : bpp;
	cnt = 0;
	for (y = 0; y < h && cnt < 256; y++) {
		uae_u32 *s = (uae_u32*)(((uae_u8*)bmp.bmBits) + y * bmpw);
		for (x = 0; x < w && cnt < 256; x++) {
			uae_u32 v = s[x];
			for (i = 0; i < cnt; i++) {
				if (colors[i] == v)
					break;
			}
			if (i == 256)
				break;
			if (i == cnt)
				colors[cnt++] = v;
		}
	}
	if (cnt < 256) {
		i = cnt;
		iffbpp = 0;
		while (i > 0) {
			i >>= 1;
			iffbpp++;
		}
#if DEBUG_CLIP > 0
		write_log (_T("BMP2IFF: Colors=%d BPP=%d\n"), cnt, iffbpp);
#endif
	}

	bodysize = (((w + 15) & ~15) / 8) * h * iffbpp;

	iffsize = sizeof (iffilbm) + (8 + 256 * 3 + 1) + (4 + 4) + bodysize;
	iff = xcalloc (uae_u8, iffsize);
	memcpy (iff, iffilbm, sizeof iffilbm);
	p = iff + 5 * 4;
	// BMHD
	p[0] = w >> 8;
	p[1] = w;
	p[2] = h >> 8;
	p[3] = h;
	p[8] = iffbpp;
	p[14] = 1;
	p[15] = 1;
	p[16] = w >> 8;
	p[17] = w;
	p[18] = h >> 8;
	p[19] = h;
	p = iff + sizeof iffilbm - 4;
	// CAMG
	if (w > 400)
		p[2] |= 0x80; // HIRES
	if (h > 300)
		p[3] |= 0x04; // LACE
	p += 4;
	if (iffbpp <= 8) {
		int cols = 1 << iffbpp;
		int cnt = 0;
		memcpy (p, "CMAP", 4);
		p[4] = 0;
		p[5] = 0;
		p[6] = (cols * 3) >> 8;
		p[7] = (cols * 3);
		p += 8;
		for (i = 0; i < cols; i++) {
			*p++ = colors[i] >> 16;
			*p++ = colors[i] >> 8;
			*p++ = colors[i] >> 0;
			cnt += 3;
		}
		if (cnt & 1)
			*p++ = 0;
	}
	memcpy (p, "BODY", 4);
	p[4] = bodysize >> 24;
	p[5] = bodysize >> 16;
	p[6] = bodysize >>  8;
	p[7] = bodysize >>  0;
	p += 8;

	if (bpp > 8 && iffbpp <= 8) {
		for (y = 0; y < h && i < 256; y++) {
			uae_u32 *s = (uae_u32*)(((uae_u8*)bmp.bmBits) + y * bmpw);
			int b;
			for (b = 0; b < iffbpp; b++) {
				int mask2 = 1 << b;
				for (x = 0; x < w; x++) {
					uae_u32 v = s[x];
					int off = x / 8;
					int mask = 1 << (7 - (x & 7));
					for (i = 0; i < (1 << iffbpp); i++) {
						if (colors[i] == v)
							break;
					}
					if (i & mask2)
						p[off] |= mask;
				}
				p += ((w + 15) & ~15) / 8;
			}
		}
	} else if (bpp <= 8) {
		for (y = 0; y < h; y++) {
			uae_u8 *s = (uae_u8*)(((uae_u8*)bmp.bmBits) + y * bmpw);
			int b;
			for (b = 0; b < 8; b++) {
				int mask2 = 1 << b;
				for (x = 0; x < w; x++) {
					int off = x / 8;
					int mask = 1 << (7 - (x & 7));
					uae_u8 v = s[x];
					if (v & mask2)
						p[off] |= mask;
				}
				p += ((w + 15) & ~15) / 8;
			}
		}
	} else if (bpp == 32) {
		for (y = 0; y < h; y++) {
			uae_u32 *s = (uae_u32*)(((uae_u8*)bmp.bmBits) + y * bmpw);
			int b, bb;
			for (bb = 0; bb < 3; bb++) {
				for (b = 0; b < 8; b++) {
					int mask2 = 1 << (((2 - bb) * 8) + b);
					for (x = 0; x < w; x++) {
						int off = x / 8;
						int mask = 1 << (7 - (x & 7));
						uae_u32 v = s[x];
						if (v & mask2)
							p[off] |= mask;
					}
					p += ((w + 15) & ~15) / 8;
				}
			}
		}
	}

	tsize = p - iff - 8;
	p = iff + 4;
	p[0] = tsize >> 24;
	p[1] = tsize >> 16;
	p[2] = tsize >>  8;
	p[3] = tsize >>  0;

	to_amiga_size = 8 + tsize + (tsize & 1);
	to_amiga = iff;

	to_amiga_start ();

	xfree (bmp.bmBits);
}

static uae_u8 *iff_decomp (const uae_u8 *addr, int w, int h, int planes)
{
	int y, i, w2;
	uae_u8 *dst;

	w2 = (w + 15) & ~15;
	dst = xmalloc (uae_u8, w2 * h * planes);
	for (y = 0; y < h * planes; y++) {
		uae_u8 *p = dst + w2 * y;
		uae_u8 *end = p + w2;
		while (p < end) {
			uae_s8 c = *addr++;
			if (c >= 0 && c <= 127) {
				uae_u8 cnt = c + 1;
				for (i = 0; i < cnt && p < end; i++)
					*p++= *addr++;
			} else if (c <= -1 && c >= -127) {
				uae_u8 cnt = -c + 1;
				uae_u8 v = *addr++;
				for (i = 0; i < cnt && p < end; i++)
					*p++= v;
			}
		}
	}
	return dst;
}

static int clipboard_put_bmp (HBITMAP hbmp);
static void from_iff_ilbm (uaecptr ilbm, uae_u32 len)
{
	HBITMAP hbm = NULL;
	BITMAPINFO *bmih;
	uae_u32 size, bmsize, camg;
	uae_u8 *addr, *eaddr, *bmptr;
	int i;
	int w, bmpw, iffw, h, planes, compr, masking;
	int bmhd, body;
	RGBQUAD rgbx[256];

	bmih = NULL;
	bmhd = 0, body = 0;
	bmsize = 0;
	bmptr = NULL;
	planes = 0; compr = 0;
	addr = get_real_address (ilbm);
	eaddr = addr + len;
	size = (addr[4] << 24) | (addr[5] << 16) | (addr[6] << 8) | (addr[7] << 0);
	if (memcmp ("ILBM", addr + 8, 4))
		return;
	camg = 0;
	for (i = 0; i < 256; i++) {
		rgbx[i].rgbRed = i;
		rgbx[i].rgbGreen = i;
		rgbx[i].rgbBlue = i;
		rgbx[i].rgbReserved = 0;
	}

	addr += 12;
	for (;;) {
		int csize;
		uae_u8 chunk[4];
		uae_u8 *paddr, *ceaddr;

		paddr = addr;
		memcpy (chunk, addr, 4);
		csize = (addr[4] << 24) | (addr[5] << 16) | (addr[6] << 8) | (addr[7] << 0);
		addr += 8;
		ceaddr = addr + csize;
		if (!memcmp (chunk, "BMHD" ,4)) {
			bmhd = 1;
			w = (addr[0] << 8) | addr[1];
			h = (addr[2] << 8) | addr[3];
			planes = addr[8];
			masking = addr[9];
			compr = addr[10];

		} else if (!memcmp (chunk, "CAMG" ,4)) {
			camg = (addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | (addr[3] << 0);
			if ((camg & 0xFFFF0000) && !(camg & 0x1000))
				camg = 0;
		} else if (!memcmp (chunk, "CMAP" ,4)) {
			if (planes <= 8) {
				int zero4 = 1;
				for (i = 0; i < (1 << planes) && addr < ceaddr; i++, addr += 3) {
					rgbx[i].rgbRed = addr[0];
					rgbx[i].rgbGreen = addr[1];
					rgbx[i].rgbBlue = addr[2];
					if ((addr[0] & 0x0f) || (addr[1] & 0x0f) || (addr[2] & 0x0f))
						zero4 = 0;
				}
				if (zero4) {
					for (i = 0; i < (1 << planes); i++) {
						rgbx[i].rgbRed |= rgbx[i].rgbRed >> 4;
						rgbx[i].rgbGreen |= rgbx[i].rgbGreen >> 4;
						rgbx[i].rgbBlue |= rgbx[i].rgbBlue >> 4;
					}
				}
			}
		} else if (!memcmp (chunk, "BODY" ,4) && bmhd) {
			int x, y;
			int ham, ehb, bmpdepth;
			uae_u8 *caddr = NULL, *dptr;
			body = 1;

#if DEBUG_CLIP > 0
			write_log (_T("W=%d H=%d planes=%d mask=%d comp=%d CAMG=%08x\n"), w, h, planes, masking, compr, camg);
#endif

			ham = 0; ehb = 0;
			if ((camg & 0x0800) && planes > 4)
				ham = planes >= 7 ? 8 : 6;
			if (!(camg & (0x0800 | 0x0400 | 0x8000 | 0x0040)) && (camg & 0x0080) && planes == 6)
				ehb = 1;

			if (planes <= 8 && !ham)
				bmpdepth = 8;
			else
				bmpdepth = 32;
			iffw = (w + 15) & ~15;
			bmpw = (w * (bmpdepth / 8) + 3) & ~3;

			bmsize = sizeof (BITMAPINFO);
			if (bmpdepth <= 8)
				bmsize += (1 << planes) * sizeof (RGBQUAD);
			bmih = (BITMAPINFO*)xcalloc (uae_u8, bmsize);
			bmih->bmiHeader.biSize = sizeof (bmih->bmiHeader);
			bmih->bmiHeader.biWidth = w;
			bmih->bmiHeader.biHeight = -h;
			bmih->bmiHeader.biPlanes = 1;
			bmih->bmiHeader.biBitCount = bmpdepth;
			bmih->bmiHeader.biCompression = BI_RGB;
			if (bmpdepth <= 8) {
				RGBQUAD *rgb = bmih->bmiColors;
				bmih->bmiHeader.biClrImportant = 0;
				bmih->bmiHeader.biClrUsed = 1 << (planes + ehb);
				for (i = 0; i < (1 << planes); i++) {
					rgb->rgbRed = rgbx[i].rgbRed;
					rgb->rgbGreen = rgbx[i].rgbGreen;
					rgb->rgbBlue = rgbx[i].rgbBlue;
					rgb++;
				}
				if (ehb) {
					for (i = 0; i < (1 << planes); i++) {
						rgb->rgbRed = rgbx[i].rgbRed >> 1;
						rgb->rgbGreen = rgbx[i].rgbGreen >> 1;
						rgb->rgbBlue = rgbx[i].rgbBlue >> 1;
						rgb++;
					}
				}
			}
			bmptr = xcalloc (uae_u8, bmpw * h);

			if (compr)
				addr = caddr = iff_decomp (addr, w, h, planes + (masking == 1 ? 1 : 0));
			dptr = bmptr;

			if (planes <= 8 && !ham) {
				// paletted
				for (y = 0; y < h; y++) {
					for (x = 0; x < w; x++) {
						int b;
						int off = x / 8;
						int mask = 1 << (7 - (x & 7));
						uae_u8 c = 0;
						for (b = 0; b < planes; b++)
							c |= (addr[b * iffw / 8 + off] & mask) ? (1 << b) : 0;
						dptr[x] = c;
					}
					dptr += bmpw;
					addr += planes * iffw / 8;
					if (masking == 1)
						addr += iffw / 8;
				}
			} else if (ham) {
				// HAM6/8 -> 32
				for (y = 0; y < h; y++) {
					DWORD ham_lastcolor = 0;
					for (x = 0; x < w; x++) {
						int b;
						int off = x / 8;
						int mask = 1 << (7 - (x & 7));
						uae_u8 c = 0;
						for (b = 0; b < planes; b++)
							c |= (addr[b * iffw / 8 + off] & mask) ? (1 << b) : 0;
						if (ham > 6) {
							DWORD c2 = c & 0x3f;
							switch (c >> 6)
							{
							case 0: ham_lastcolor = *((DWORD*)(&rgbx[c])); break;
							case 1: ham_lastcolor &= 0x00FFFF03; ham_lastcolor |= c2 << 2; break;
							case 2: ham_lastcolor &= 0x0003FFFF; ham_lastcolor |= c2 << 18; break;
							case 3: ham_lastcolor &= 0x00FF03FF; ham_lastcolor |= c2 << 10; break;
							}
						} else {
							uae_u32 c2 = c & 0x0f;
							switch (c >> 4)
							{
							case 0: ham_lastcolor = *((DWORD*)(&rgbx[c])); break;
							case 1: ham_lastcolor &= 0xFFFF00; ham_lastcolor |= c2 << 4; break;
							case 2: ham_lastcolor &= 0x00FFFF; ham_lastcolor |= c2 << 20; break;
							case 3: ham_lastcolor &= 0xFF00FF; ham_lastcolor |= c2 << 12; break;
							}
						}
						*((DWORD*)(&dptr[x * 4])) = ham_lastcolor;
					}
					dptr += bmpw;
					addr += planes * iffw / 8;
					if (masking == 1)
						addr += iffw / 8;
				}
			} else {
				// 24bit RGB
				for (y = 0; y < h; y++) {
					for (x = 0; x < w; x++) {
						uae_u8 c;
						int b;
						int off = x / 8;
						int mask = 1 << (7 - (x & 7));
						c = 0;
						for (b = 0; b < planes / 3; b++)
							c |= (addr[((0 * planes / 3) + b) * iffw / 8 + off] & mask) ? (1 << b) : 0;
						dptr[x * 4 + 2] = c;
						c = 0;
						for (b = 0; b < planes / 3; b++)
							c |= (addr[((1 * planes / 3) + b) * iffw / 8 + off] & mask) ? (1 << b) : 0;
						dptr[x * 4 + 1] = c;
						c = 0;
						for (b = 0; b < planes / 3; b++)
							c |= (addr[((2 * planes / 3) + b) * iffw / 8 + off] & mask) ? (1 << b) : 0;
						dptr[x * 4 + 0] = c;
					}
					dptr += bmpw;
					addr += planes * iffw / 8;
					if (masking == 1)
						addr += iffw / 8;
				}
			}
			if (caddr)
				xfree (caddr);
		}
		addr = paddr + csize + (csize & 1) + 8;
		if (addr >= eaddr)
			break;
	}
	if (body) {
		hbm = CreateDIBitmap (hdc, &bmih->bmiHeader, CBM_INIT, bmptr, bmih, DIB_RGB_COLORS);
		if (hbm)
			clipboard_put_bmp (hbm);
	}
	xfree (bmih);
	xfree (bmptr);
}

static void from_iff (uaecptr data, uae_u32 len)
{
	uae_u8 *addr;

	if (len < 18)
		return;
	if (!valid_address (data, len))
		return;
	addr = get_real_address (data);
	if (clipboard_debug)
		debugwrite (_T("clipboard_a2p"), addr, len);
	if (memcmp ("FORM", addr, 4))
		return;
	if (!memcmp ("FTXT", addr + 8, 4))
		from_iff_text (data, len);
	if (!memcmp ("ILBM", addr + 8, 4))
		from_iff_ilbm (data, len);
}

void clipboard_disable (bool disabled)
{
	clip_disabled = disabled;
}

static void clipboard_read (HWND hwnd)
{
	HGLOBAL hglb;
	UINT f;
	int text = FALSE, bmp = FALSE;

	if (clip_disabled)
		return;
	if (to_amiga) {
#if DEBUG_CLIP > 0
		write_log (_T("clipboard: read windows clipboard but ignored because previous clip transfer still active\n"));
#endif
		return;
	}
	clipboard_change = 0;
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: read windows clipboard\n"));
#endif
	if (!OpenClipboard (hwnd))
		return;
	f = 0;
	while (f = EnumClipboardFormats (f)) {
		if (f == CF_UNICODETEXT)
			text = TRUE;
		if (f == CF_BITMAP)
			bmp = TRUE;
	}
	if (text) {
		hglb = GetClipboardData (CF_UNICODETEXT); 
		if (hglb != NULL) { 
			TCHAR *lptstr = (TCHAR*)GlobalLock (hglb); 
			if (lptstr != NULL) {
#if DEBUG_CLIP > 0
				write_log (_T("clipboard: CF_UNICODETEXT '%s'\n"), lptstr);
#endif
				to_iff_text (lptstr);
				GlobalUnlock (hglb);
			}
		}
	} else if (bmp) {
		HBITMAP hbmp = (HBITMAP)GetClipboardData (CF_BITMAP);
		if (hbmp != NULL) {
#if DEBUG_CLIP > 0
			write_log (_T("clipboard: CF_BITMAP\n"));
#endif
			to_iff_ilbm (hbmp);
		}
	}
	CloseClipboard ();
}

static void clipboard_free_delayed (void)
{
	if (clipboard_delayed_data == 0)
		return;
	if (clipboard_delayed_size < 0)
		xfree (clipboard_delayed_data);
	else
		DeleteObject (clipboard_delayed_data);
	clipboard_delayed_data = 0;
	clipboard_delayed_size = 0;
}

void clipboard_changed (HWND hwnd)
{
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: windows clipboard changed message\n"));
#endif
	if (!clipboard_data || !initialized)
		return;
	if (clipopen)
		return;
	if (!clipactive) {
		clipboard_change = 1;
		return;
	}
	clipboard_read (hwnd);
}

static int clipboard_put_bmp_real (HBITMAP hbmp)
{
	int ret = FALSE;

	if (!OpenClipboard (chwnd)) 
		return ret;
	clipopen++;
	EmptyClipboard ();
	SetClipboardData (CF_BITMAP, hbmp); 
	ret = TRUE;
	CloseClipboard ();
	clipopen--;
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: BMP written to windows clipboard\n"));
#endif
	return ret;
}

static int clipboard_put_text_real (const TCHAR *txt)
{
	HGLOBAL hglb;
	int ret = FALSE;

	if (!OpenClipboard (chwnd)) 
		return ret;
	clipopen++;
	EmptyClipboard (); 
	hglb = GlobalAlloc (GMEM_MOVEABLE, (_tcslen (txt) + 1) * sizeof (TCHAR));
	if (hglb) {
		TCHAR *lptstr = (TCHAR*)GlobalLock (hglb);
		_tcscpy (lptstr, txt);
		GlobalUnlock (hglb);
		SetClipboardData (CF_UNICODETEXT, hglb); 
		ret = TRUE;
	}
	CloseClipboard ();
	clipopen--;
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: text written to windows clipboard\n"));
#endif
	return ret;
}

static int clipboard_put_text (const TCHAR *txt)
{
	if (!clipactive)
		return clipboard_put_text_real (txt);
	clipboard_free_delayed ();
	clipboard_delayed_data = my_strdup (txt);
	clipboard_delayed_size = -1;
	return 1;
}

static int clipboard_put_bmp (HBITMAP hbmp)
{
	if (!clipactive)
		return clipboard_put_bmp_real (hbmp);
	clipboard_delayed_data = (void*)hbmp;
	clipboard_delayed_size = 1;
	return 1;
}

void amiga_clipboard_die (void)
{
	signaling = 0;
	write_log (_T("clipboard not initialized\n"));
}

void amiga_clipboard_init (void)
{
	signaling = 0;
	write_log (_T("clipboard initialized\n"));
	initialized = 1;
	clipboard_read (chwnd);
}

void amiga_clipboard_task_start (uaecptr data)
{
	clipboard_data = data;
	signaling = 1;
	write_log (_T("clipboard task init: %08x\n"), clipboard_data);
}

uae_u32 amiga_clipboard_proc_start (void)
{
	write_log (_T("clipboard process init: %08x\n"), clipboard_data);
	signaling = 1;
	return clipboard_data;
}

void amiga_clipboard_got_data (uaecptr data, uae_u32 size, uae_u32 actual)
{
	uae_u8 *addr;
	if (!initialized) {
		write_log (_T("clipboard: got_data() before initialized!?\n"));
		return;
	}
	addr = get_real_address (data);
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: <-amiga, %08x, %08x %d %d\n"), clipboard_data, data, size, actual);
#endif
	from_iff (data, actual);
}

int amiga_clipboard_want_data (void)
{
	uae_u32 addr, size;

	addr = get_long (clipboard_data + 4);
	size = get_long (clipboard_data);
	if (!initialized) {
		write_log (_T("clipboard: want_data() before initialized!? (%08x %08x %d)\n"), clipboard_data, addr, size);
		to_amiga = NULL;
		return 0;
	}
	if (size != to_amiga_size) {
		write_log (_T("clipboard: size %d <> %d mismatch!?\n"), size, to_amiga_size);
		to_amiga = NULL;
		return 0;
	}
	if (addr && size) {
		uae_u8 *raddr = get_real_address (addr);
		memcpy (raddr, to_amiga, size);
	}
	xfree (to_amiga);
#if DEBUG_CLIP > 0
	write_log (_T("clipboard: ->amiga, %08x, %08x %d (%d) bytes\n"), clipboard_data, addr, size, to_amiga_size);
#endif
	to_amiga = NULL;
	to_amiga_size = 0;
	return 1;
}

void clipboard_active (HWND hwnd, int active)
{
	clipactive = active;
	if (!initialized)
		return;
	if (clipactive && clipboard_change) {
		clipboard_read (hwnd);
	}
	if (!clipactive && clipboard_delayed_data) {
		if (clipboard_delayed_size < 0) {
			clipboard_put_text_real ((TCHAR*)clipboard_delayed_data);
			xfree (clipboard_delayed_data);
		} else {
			clipboard_put_bmp_real ((HBITMAP)clipboard_delayed_data);
		}
		clipboard_delayed_data = NULL;
		clipboard_delayed_size = 0;
	}
}

void clipboard_vsync (void)
{
	uaecptr task;

	if (!signaling || !clipboard_data)
		return;
	vdelay--;
	if (vdelay > 0)
		return;
	task = get_long (clipboard_data + 8);
	if (task && native2amiga_isfree ()) {
		uae_Signal (task, 1 << 13);
#if DEBUG_CLIP > 0
		write_log (_T("clipboard: signal %08x\n"), clipboard_data);
#endif
	}
	vdelay = 50;
}

void clipboard_reset (void)
{
	write_log (_T("clipboard: reset (%08x)\n"), clipboard_data);
	vdelay = 100;
	clipboard_free_delayed ();
	clipboard_data = 0;
	signaling = 0;
	initialized = 0;
	xfree (to_amiga);
	to_amiga = NULL;
	to_amiga_size = 0;
	clip_disabled = false;
	ReleaseDC (chwnd, hdc);
}

void clipboard_init (HWND hwnd)
{
	chwnd = hwnd;
	hdc = GetDC (chwnd);
}
