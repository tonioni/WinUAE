#include "sysconfig.h"
#include "sysdeps.h"
#ifdef GFXFILTER
#include "options.h"
#include "xwin.h"
#include "gfxfilter.h"
#include "od-generic/scaler.h"

void getfilterdata(int monid, displayscale *ds)
{
 host_scaler host = {};
 host.monitor_id = monid;
 host.rtg = adisplays[monid].picasso_on;
 // SDL windows resize independently of the configured window dimensions.
 host.integer_width = ds->dstwidth;
 host.integer_height = ds->dstheight;
 host_getfilterdata(monid, ds, host);
}
#endif
