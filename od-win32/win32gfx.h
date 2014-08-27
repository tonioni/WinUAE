#ifndef __WIN32GFX_H__
#define __WIN32GFX_H__

#include <ddraw.h>

#define RTG_MODE_SCALE 1
#define RTG_MODE_CENTER 2
#define RTG_MODE_INTEGER_SCALE 3

extern void sortdisplays (void);
extern void enumeratedisplays (void);

int WIN32GFX_IsPicassoScreen (void);
int WIN32GFX_GetWidth (void);
int WIN32GFX_GetHeight(void);
int WIN32GFX_GetDepth (int real);
void WIN32GFX_DisplayChangeRequested (int);
void DX_Invalidate (int x, int y, int width, int height);

RGBFTYPE WIN32GFX_FigurePixelFormats (RGBFTYPE colortype);
int WIN32GFX_AdjustScreenmode (struct MultiDisplay *md, int *pwidth, int *pheight, int *ppixbits);
extern HCURSOR normalcursor;

extern HWND hStatusWnd;
extern int default_freq;
extern int normal_display_change_starting;
extern int window_led_drives, window_led_drives_end;
extern int window_led_hd, window_led_hd_end;
extern int window_led_joys, window_led_joys_end, window_led_joy_start;
extern int window_led_msg, window_led_msg_end, window_led_msg_start;
extern int scalepicasso;

extern HDC gethdc (void);
extern void releasehdc (HDC hdc);
extern void close_windows (void);
extern void updatewinfsmode (struct uae_prefs *p);
extern int is3dmode (void);
extern void gfx_lock (void);
extern void gfx_unlock (void);

extern bool lockscr3d(struct vidbuffer *vb);
extern void unlockscr3d(struct vidbuffer *vb);

void DX_Fill (int dstx, int dsty, int width, int height, uae_u32 color);
void DX_Blit (int x, int y, int w, int h);
void centerdstrect (RECT *);
struct MultiDisplay *getdisplay (struct uae_prefs *p);
double getcurrentvblankrate (void);
void vblank_reset (double freq);
extern int getrefreshrate (int width, int height);
#endif
