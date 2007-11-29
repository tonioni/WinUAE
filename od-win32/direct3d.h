
extern void D3D_resize (int width, int height);
extern void D3D_free ();
extern const char *D3D_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth);
extern void D3D_render (void);
extern void D3D_getpixelformat (int depth,int *rb, int *bb, int *gb, int *rs, int *bs, int *gs, int *ab, int *ar, int *a);
extern void D3D_refresh (void);
extern int D3D_locktexture(void);
extern void D3D_unlocktexture(void);
extern void D3D_guimode (int guion);
extern HDC D3D_getDC(HDC hdc);
extern int D3D_isenabled (void);
extern int D3D_needreset (void);
