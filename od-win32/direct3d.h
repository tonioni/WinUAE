extern void D3D_resize (int width, int height);
extern void D3D_free (void);
extern const TCHAR *D3D_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth, int mult);
extern void D3D_getpixelformat (int depth,int *rb, int *bb, int *gb, int *rs, int *bs, int *gs, int *ab, int *ar, int *a);
extern void D3D_refresh (void);
extern void D3D_flip (void);
extern uae_u8 *D3D_locktexture(int*,int);
extern void D3D_unlocktexture(void);
extern void D3D_flushtexture (int miny, int maxy);
extern void D3D_guimode (int guion);
extern HDC D3D_getDC(HDC hdc);
extern int D3D_isenabled (void);
extern int D3D_needreset (void);
extern void D3D_clear (void);
extern int D3D_canshaders (void);
extern int D3D_goodenough (void);
extern void D3D_setcursor (int x, int y, int visible);
extern LPDIRECT3DTEXTURE9 cursorsurfaced3d;

#define CURSORMAXWIDTH 64
#define CURSORMAXHEIGHT 64
