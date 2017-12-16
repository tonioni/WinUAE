
extern void(*D3D_free)(bool immediate);
extern const TCHAR* (*D3D_init)(HWND ahwnd, int w_w, int h_h, int depth, int *freq, int mmult);
extern bool(*D3D_alloctexture)(int, int);
extern void(*D3D_refresh)(void);
extern bool(*D3D_renderframe)(bool);
extern void(*D3D_showframe)(void);
extern void(*D3D_showframe_special)(int);
extern uae_u8* (*D3D_locktexture)(int*, int*, bool);
extern void(*D3D_unlocktexture)(void);
extern void(*D3D_flushtexture)(int miny, int maxy);
extern void(*D3D_guimode)(int);
extern HDC(*D3D_getDC)(HDC hdc);
extern int(*D3D_isenabled)(void);
extern void(*D3D_clear)(void);
extern int(*D3D_canshaders)(void);
extern int(*D3D_goodenough)(void);
extern bool(*D3D_setcursor)(int x, int y, int width, int height, bool visible, bool noscale);
extern uae_u8* (*D3D_setcursorsurface)(int *pitch);
extern bool(*D3D_getvblankpos)(int *vpos);
extern double(*D3D_getrefreshrate)(void);
extern void(*D3D_vblank_reset)(double freq);
extern void(*D3D_restore)(void); 
extern void(*D3D_resize)(int);
extern void(*D3D_change)(int);
extern bool(*D3D_getscalerect)(float *mx, float *my, float *sx, float *sy);

extern LPDIRECT3DSURFACE9 D3D_capture(int*,int*,int*);
extern bool D3D11_capture(void**,int*, int*,int*);

void D3D_getpixelformat(int depth, int *rb, int *gb, int *bb, int *rs, int *gs, int *bs, int *ab, int *as, int *a);

void d3d9_select(void);
void d3d11_select(void);
void d3d_select(struct uae_prefs *p);
int can_D3D11(bool checkdevice);

#define CURSORMAXWIDTH 64
#define CURSORMAXHEIGHT 64
