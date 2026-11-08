#ifndef UAE_PRINTER_HOST_H
#define UAE_PRINTER_HOST_H

#include "uae/types.h"

struct host_printer_glyph {
	uae_u8 *bitmap;
	int left, top, ascender, width, rows, pitch, advance;
	int overscore_offset, overscore_double_offset;
};

bool host_printer_init(const TCHAR *name, uae_u16 *dpi_x, uae_u16 *dpi_y,
	float *width, float *height);
void host_printer_finish_init();
void host_printer_close();
void host_printer_reset_font();
void host_printer_set_font(const TCHAR *name, float horizontal, float vertical,
	uae_u16 dpi_x, uae_u16 dpi_y, bool italic);
bool host_printer_render_glyph(uae_u16 character, uae_u16 style,
	host_printer_glyph *glyph);
void host_printer_release_glyph(host_printer_glyph *glyph);

using host_printer_color = void (*)(uae_u8 *, uae_u8 *, int, int, int,
	uae_u8 *, uae_u8 *, uae_u8 *);
// Return false when the shared PNG writer should handle the page.
bool host_printer_output_page(uae_u8 *page, uae_u8 *colors, int width,
	int height, int pitch, bool colored, host_printer_color get_color);
void host_printer_end_document();
void host_printer_preview_file(const TCHAR *name);

#endif /* UAE_PRINTER_HOST_H */
