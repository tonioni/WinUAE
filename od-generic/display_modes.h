#ifndef UAE_DISPLAY_MODES_H
#define UAE_DISPLAY_MODES_H

struct ScreenResolution {
    uae_u32 width;  /* in pixels */
    uae_u32 height; /* in pixels */
};

#define MAX_PICASSO_MODES 300
#define MAX_REFRESH_RATES 100

#define REFRESH_RATE_RAW 1
#define REFRESH_RATE_LACE 2

struct PicassoResolution {
    bool inuse;
    struct ScreenResolution res;
    int residx;
    int refresh[MAX_REFRESH_RATES];	/* refresh-rates in Hz */
    int refreshtype[MAX_REFRESH_RATES]; /* 0=normal,1=raw,2=lace */
    TCHAR name[25];
    int rawmode;
    bool lace; // all modes lace
};

bool host_display_mode_add(PicassoResolution *modes, int capacity, int width, int height,
			   int refresh, int rawmode, bool lace);
int host_display_modes_sort(PicassoResolution *modes, int capacity, bool refreshes);
#endif
