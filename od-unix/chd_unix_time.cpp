#include "sysconfig.h"
#include "sysdeps.h"

#include "archivers/chd/chdtypes.h"
#include "archivers/chd/osdcore.h"

#include <chrono>
#include <thread>

static const osd_ticks_t unix_chd_ticks_per_second = 1000000000ULL;

osd_ticks_t osd_ticks(void)
{
    using clock = std::chrono::steady_clock;
    return (osd_ticks_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        clock::now().time_since_epoch()).count();
}

osd_ticks_t osd_ticks_per_second(void)
{
    return unix_chd_ticks_per_second;
}

void osd_sleep(osd_ticks_t duration)
{
    if (duration == 0) {
        return;
    }
    std::this_thread::sleep_for(std::chrono::nanoseconds(duration));
}
