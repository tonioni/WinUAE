#ifndef __WIN32GFX_H__
#define __WIN32GFX_H__

#include <ddraw.h>

extern void sortdisplays (void);
extern void enumeratedisplays (int);

int WIN32GFX_IsPicassoScreen( void );
int WIN32GFX_GetWidth( void );
int WIN32GFX_GetHeight( void );
int WIN32GFX_GetDepth (int real);
void WIN32GFX_DisplayChangeRequested( void );
void WIN32GFX_ToggleFullScreen( void );
void WIN32GFX_DisablePicasso( void );
void WIN32GFX_EnablePicasso( void );
void WIN32GFX_PaletteChange( void );
int WIN32GFX_ClearPalette( void );
int WIN32GFX_SetPalette( void );
void WIN32GFX_WindowMove ( void );

int DX_Blit( int srcx, int srcy, int dstx, int dsty, int width, int height, BLIT_OPCODE opcode );

#ifndef _WIN32_WCE
RGBFTYPE WIN32GFX_FigurePixelFormats( RGBFTYPE colortype );
int WIN32GFX_AdjustScreenmode( uae_u32 *pwidth, uae_u32 *pheight, uae_u32 *ppixbits );
#endif

extern HWND hStatusWnd;
extern HINSTANCE hDDraw;
extern char *start_path;
extern uae_u32 default_freq;

extern HDC gethdc (void);
extern void releasehdc (HDC hdc);
extern void close_windows (void);
extern void updatewinfsmode (struct uae_prefs *p);
extern int is3dmode (void);

#endif
