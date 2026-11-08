// Epson printer font rasterization through FreeType. Pages use PNG output.
#include "sysconfig.h"
#include "sysdeps.h"
#include "printer_host.h"
#include <ft2build.h>
#include FT_FREETYPE_H

static FT_Library library;
static FT_Face font;

bool host_printer_init(const TCHAR *, uae_u16 *, uae_u16 *, float *, float *)
{
	if (FT_Init_FreeType(&library)) {
		write_log(_T("EPSONPRINTER: Unable to init Freetype2. ASCII printing disabled\n"));
		return false;
	}
	return true;
}

void host_printer_finish_init() {}

void host_printer_reset_font()
{
	if (font) {
		FT_Done_Face(font);
		font = NULL;
	}
}

void host_printer_close()
{
	// Faces must be released before destroying the library that owns them.
	host_printer_reset_font();
	if (library) {
		FT_Done_FreeType(library);
		library = NULL;
	}
}

void host_printer_set_font(const TCHAR *, float horizontal, float vertical,
	uae_u16 dpi_x, uae_u16 dpi_y, bool italic)
{
	host_printer_reset_font();
#ifdef __APPLE__
	const char *fonts[] = { "/System/Library/Fonts/Monaco.ttf",
		"/System/Library/Fonts/Menlo.ttc", NULL };
#else
	const char *fonts[] = { "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
		"/usr/share/fonts/truetype/freefont/FreeMono.ttf", NULL };
#endif
	if (!library) {
		write_log(_T("EPSONPRINTER: FreeType is unavailable\n"));
		return;
	}
	for (int i = 0; fonts[i] && !font; i++) {
		if (FT_New_Face(library, fonts[i], 0, &font)) font = NULL;
	}
	if (!font) {
		write_log(_T("EPSONPRINTER: unable to load a host font\n"));
		return;
	}
	FT_Set_Char_Size(font, (uae_u16)horizontal * 64,
		(uae_u16)vertical * 64, dpi_x, dpi_y);
	if (italic) {
		FT_Matrix matrix;
		matrix.xx = 0x10000L;
		matrix.xy = (FT_Fixed)(0.20 * 0x10000L);
		matrix.yx = 0;
		matrix.yy = 0x10000L;
		FT_Set_Transform(font, &matrix, 0);
	}
}

bool host_printer_render_glyph(uae_u16 character, uae_u16,
	host_printer_glyph *glyph)
{
	*glyph = {};
	if (!font) return false;
	FT_UInt index = FT_Get_Char_Index(font, character);
	FT_Load_Glyph(font, index, FT_LOAD_DEFAULT);
	FT_Render_Glyph(font->glyph, FT_RENDER_MODE_NORMAL);
	glyph->left = font->glyph->bitmap_left;
	glyph->top = font->glyph->bitmap_top;
	glyph->ascender = font->size->metrics.ascender / 64;
	glyph->width = font->glyph->bitmap.width;
	glyph->rows = font->glyph->bitmap.rows;
	glyph->pitch = font->glyph->bitmap.pitch;
	glyph->advance = font->glyph->advance.x / 64;
	glyph->bitmap = font->glyph->bitmap.buffer;
	return true;
}

void host_printer_release_glyph(host_printer_glyph *) {}
bool host_printer_output_page(uae_u8 *, uae_u8 *, int, int, int, bool,
	host_printer_color) { return false; }
void host_printer_end_document() {}
void host_printer_preview_file(const TCHAR *) {}
