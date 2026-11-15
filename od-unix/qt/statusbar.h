#pragma once

#include "include/statusbar.h"

void ensureWinUaeQtApplication();
int winUaeQtStatusbarHeight();
void winUaeQtPaintStatusbar(unsigned int *pixels, int width, int height,
    float scale, const statusbar_state &state, const TCHAR *message,
    statusbar_rect *rects, statusbar_item *items);
void winUaeQtStatusbarTooltip(const char *text, int x, int y);
