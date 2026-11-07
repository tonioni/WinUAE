#include "sysconfig.h"
#include "sysdeps.h"
#include "display_modes.h"

bool host_display_mode_add(PicassoResolution *modes, int capacity, int w, int h, int freq,
			   int rawmode, bool lace)
{
    if (!modes || capacity <= 1 || w <= 0 || h <= 0)
	return false;
    int i, j;
    i = 0;
    while (i < capacity && modes[i].inuse) {
	if (modes[i].res.width == w && modes[i].res.height == h) {
	    for (j = 0; j < MAX_REFRESH_RATES - 1; j++) {
		if (modes[i].refresh[j] == 0 || modes[i].refresh[j] == freq)
		    break;
	    }
	    if (j < MAX_REFRESH_RATES - 1) {
		bool append = modes[i].refresh[j] == 0;
		modes[i].refresh[j] = freq;
		modes[i].refreshtype[j] =
		    (lace ? REFRESH_RATE_LACE : 0) | (rawmode ? REFRESH_RATE_RAW : 0);
		if (append)
		    modes[i].refresh[j + 1] = 0;
		if (!lace)
		    modes[i].lace = false;
		return true;
	    }
	    return false;
	}
	i++;
    }
    i = 0;
    while (i < capacity && modes[i].inuse) {
	i++;
    }
    if (i >= capacity - 1) {
	return false;
    }
    modes[i].inuse = true;
    modes[i].rawmode = rawmode;
    modes[i].lace = lace;
    modes[i].res.width = w;
    modes[i].res.height = h;
    modes[i].refresh[0] = freq;
    modes[i].refreshtype[0] = (lace ? REFRESH_RATE_LACE : 0) | (rawmode ? REFRESH_RATE_RAW : 0);
    modes[i].refresh[1] = 0;
    _stprintf(modes[i].name, _T("%dx%d%s"), modes[i].res.width, modes[i].res.height,
	      lace ? _T("i") : _T(""));
    return true;
}

static int _cdecl resolution_compare(const void *a, const void *b)
{
    struct PicassoResolution *ma = (struct PicassoResolution *)a;
    struct PicassoResolution *mb = (struct PicassoResolution *)b;
    if (ma->res.width < mb->res.width)
	return -1;
    if (ma->res.width > mb->res.width)
	return 1;
    if (ma->res.height < mb->res.height)
	return -1;
    if (ma->res.height > mb->res.height)
	return 1;
    return 0;
}

int host_display_modes_sort(PicassoResolution *modes, int capacity, bool refreshes)
{
    if (!modes || capacity <= 0)
	return 0;

    int i, idx = -1;
    int pw = -1, ph = -1;

    i = 0;
    while (i < capacity && modes[i].inuse) {
	i++;
    }
    qsort(modes, i, sizeof(struct PicassoResolution), resolution_compare);
    for (i = 0; i < capacity && modes[i].inuse; i++) {
	int j, k;
	for (j = 0; refreshes && j < MAX_REFRESH_RATES && modes[i].refresh[j]; j++) {
	    for (k = j + 1; k < MAX_REFRESH_RATES && modes[i].refresh[k]; k++) {
		if (modes[i].refresh[j] > modes[i].refresh[k]) {
		    int t = modes[i].refresh[j];
		    modes[i].refresh[j] = modes[i].refresh[k];
		    modes[i].refresh[k] = t;
		    t = modes[i].refreshtype[j];
		    modes[i].refreshtype[j] = modes[i].refreshtype[k];
		    modes[i].refreshtype[k] = t;
		}
	    }
	}
	if (modes[i].res.height != ph || modes[i].res.width != pw) {
	    ph = modes[i].res.height;
	    pw = modes[i].res.width;
	    idx++;
	}
	modes[i].residx = idx;
    }
    return i;
}
