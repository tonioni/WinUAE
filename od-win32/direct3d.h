
struct extoverlay
{
	int idx;
	int width, height;
	int xpos, ypos;
	uae_u8 *data;
};

extern void(*D3D_free)(int, bool immediate);
extern const TCHAR* (*D3D_init)(HWND ahwnd, int, int w_w, int h_h, int depth, int *freq, int mmulth, int mmultv);
extern bool(*D3D_alloctexture)(int, int, int);
extern void(*D3D_refresh)(int);
extern bool(*D3D_renderframe)(int, int,bool);
extern void(*D3D_showframe)(int);
extern void(*D3D_showframe_special)(int, int);
extern uae_u8* (*D3D_locktexture)(int, int*, int*, bool);
extern void(*D3D_unlocktexture)(int, int, int);
extern void(*D3D_flushtexture)(int, int miny, int maxy);
extern void(*D3D_guimode)(int, int);
extern HDC(*D3D_getDC)(int, HDC hdc);
extern int(*D3D_isenabled)(int);
extern void(*D3D_clear)(int);
extern int(*D3D_canshaders)(void);
extern int(*D3D_goodenough)(void);
extern bool(*D3D_setcursor)(int, int x, int y, int width, int height, bool visible, bool noscale);
extern uae_u8* (*D3D_setcursorsurface)(int, int *pitch);
extern float(*D3D_getrefreshrate)(int);
extern void(*D3D_restore)(int, bool);
extern void(*D3D_resize)(int, int);
extern void(*D3D_change)(int, int);
extern bool(*D3D_getscalerect)(int, float *mx, float *my, float *sx, float *sy);
extern bool(*D3D_run)(int);
extern int(*D3D_debug)(int, int);
extern void(*D3D_led)(int, int, int);
extern bool(*D3D_getscanline)(int*, bool*);
extern bool(*D3D_extoverlay)(struct extoverlay*);

extern LPDIRECT3DSURFACE9 D3D_capture(int, int*,int*,int*,bool);
extern bool D3D11_capture(int, void**,int*, int*,int*,bool);

void D3D_getpixelformat(int depth, int *rb, int *gb, int *bb, int *rs, int *gs, int *bs, int *ab, int *as, int *a);

void d3d9_select(void);
void d3d11_select(void);
void d3d_select(struct uae_prefs *p);
int can_D3D11(bool checkdevice);

#define CURSORMAXWIDTH 64
#define CURSORMAXHEIGHT 64
