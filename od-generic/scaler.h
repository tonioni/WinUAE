#ifndef UAE_HOST_SCALER_H
#define UAE_HOST_SCALER_H
struct displayscale;
struct host_scaler {
    int monitor_id;
    int integer_width, integer_height;
    bool rtg;
    bool (*shader_rect)(int, float *, float *, float *, float *, int, int);
    void (*rtg_scale)(int, displayscale *);
    void (*mode_changed)();
};
void host_getfilterdata(int monid, displayscale *ds, const host_scaler &host);
struct host_rtg_offset {
    int x, y;
    float mx, my;
};
void host_rtg_filterdata(displayscale *ds, int native_width, int native_height, int src_width,
			 int src_height, int hdouble, int vdouble, int mode, bool enabled,
			 host_rtg_offset *offset);
#endif
