int scp_open(struct zfile *zf, int drv, int *num_tracks);
void scp_close(int drv);
int scp_loadtrack(
    uae_u16 *mfmbuf, uae_u16 *tracktiming, int drv,
    int track, int *tracklength, int *multirev,
    int *gapoffset, int *nextrev, bool setrev);
void scp_loadrevolution(
    uae_u16 *mfmbuf, int drv, uae_u16 *tracktiming,
    int *tracklength);
