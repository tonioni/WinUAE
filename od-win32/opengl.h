
extern void OGL_resize (int width, int height);
extern void OGL_free ();
extern const TCHAR *OGL_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth);
extern void OGL_render (void);
extern void OGL_getpixelformat (int depth,int *rb, int *bb, int *gb, int *rs, int *bs, int *gs, int *ab, int *ar, int *a);
extern void OGL_refresh (void);
extern HDC OGL_getDC (HDC hdc);
extern int OGL_isenabled (void);
