#ifndef UAE_UNIX_RTG_OVERLAY_H
#define UAE_UNIX_RTG_OVERLAY_H

// One P96 SFT_MEMORYWINDOW, owned by the emulated uaegfx board.
struct unix_rtg_overlay_state {
    uaecptr bitmap, vram;
    uae_u32 format, modeformat, modeinfo, color;
    bool active, occlusion;
    int x, y, width, height;
    int source_width, source_height, pitch, rows;
    int clipleft, cliptop, clipwidth, clipheight, brightness;
    uae_u32 clut[256];
};

const unix_rtg_overlay_state *unix_rtg_get_overlay(int monid);

#endif
