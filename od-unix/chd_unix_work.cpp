#include "sysconfig.h"
#include "sysdeps.h"

#include "archivers/chd/chdtypes.h"
#include "archivers/chd/osdcore.h"

#include <stdlib.h>
#include <string.h>

struct osd_work_queue
{
    int flags;
    int items;
};

struct osd_work_item
{
    osd_work_queue *queue;
    osd_work_callback callback;
    void *param;
    void *result;
    int done;
};

osd_work_queue *osd_work_queue_alloc(int flags)
{
    osd_work_queue *queue = (osd_work_queue*)calloc(1, sizeof(*queue));
    if (queue) {
        queue->flags = flags;
    }
    return queue;
}

int osd_work_queue_items(osd_work_queue *queue)
{
    return queue ? queue->items : 0;
}

int osd_work_queue_wait(osd_work_queue *, osd_ticks_t)
{
    return TRUE;
}

void osd_work_queue_free(osd_work_queue *queue)
{
    free(queue);
}

osd_work_item *osd_work_item_queue_multiple(osd_work_queue *queue, osd_work_callback callback, INT32 numitems, void *parambase, INT32 paramstep, UINT32 flags)
{
    osd_work_item *last = NULL;
    if (!queue || !callback || numitems <= 0) {
        return NULL;
    }

    UINT8 *param = (UINT8*)parambase;
    for (INT32 i = 0; i < numitems; i++) {
        osd_work_item *item = (osd_work_item*)calloc(1, sizeof(*item));
        if (!item) {
            return last;
        }
        item->queue = queue;
        item->callback = callback;
        item->param = param;
        queue->items++;
        item->result = callback(param, 0);
        item->done = TRUE;
        queue->items--;

        if (flags & WORK_ITEM_FLAG_AUTO_RELEASE) {
            free(item);
        } else {
            last = item;
        }
        param += paramstep;
    }
    return (flags & WORK_ITEM_FLAG_AUTO_RELEASE) ? NULL : last;
}

int osd_work_item_wait(osd_work_item *item, osd_ticks_t)
{
    return !item || item->done;
}

void *osd_work_item_result(osd_work_item *item)
{
    return item ? item->result : NULL;
}

void osd_work_item_release(osd_work_item *item)
{
    free(item);
}
