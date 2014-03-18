
uaecptr tabletlib_startup (uaecptr resaddr);
void tabletlib_install (void);

extern void tabletlib_tablet (int x, int y, int z,
	      int pressure, uae_u32 buttonbits, int inproximity,
	      int ax, int ay, int az);
extern void tabletlib_tablet_info (int maxx, int maxy, int maxz, int maxax, int maxay, int maxaz, int xres, int yres);
