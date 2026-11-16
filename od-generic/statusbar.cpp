#include "sysconfig.h"
#include "sysdeps.h"
#include "statusbar.h"

static const int leds[STATUSBAR_ITEMS] = {
	LED_SND, LED_CPU, LED_FPS, LED_LINES, LED_POWER, LED_CAPS,
	LED_CD, LED_HD, LED_NET, LED_DF0, LED_DF1, LED_DF2, LED_DF3
};
static const int widths[STATUSBAR_ITEMS] = {
	72, 64, 64, 42, 42, 42, 24, 24, 24, 24, 24, 24, 24
};
int statusbar_led(int position)
{
	return position >= 0 && position < STATUSBAR_ITEMS ? leds[position] : -1;
}
int statusbar_position(int led)
{
	if (led == LED_MD)
		return STATUSBAR_ITEMS - 1;
	for (int i = 0; i < STATUSBAR_ITEMS; ++i)
		if (leds[i] == led)
			return i;
	return -1;
}
int statusbar_width(int position, bool ppc, bool x86)
{
	if (position < 0 || position >= STATUSBAR_ITEMS)
		return 0;
	return widths[position] + (leds[position] == LED_CPU ? 68 * (int(ppc) + int(x86)) : 0);
}
statusbar_action statusbar_action_for(int led, bool right_button)
{
	if (led >= LED_DF0 && led <= LED_DF3)
		return right_button ? STATUSBAR_EJECT : STATUSBAR_DISK;
	if (led == LED_POWER)
		return right_button ? STATUSBAR_RESET : STATUSBAR_SETTINGS;
	if (led == LED_FPS && !right_button)
		return STATUSBAR_RESUME;
	if (led == LED_CD)
		return right_button ? STATUSBAR_EJECT_CD : STATUSBAR_CD;
	return STATUSBAR_NONE;
}
int statusbar_hit(const statusbar_rect *rects, int count, float x, float y)
{
	for (int i = 0; i < count; ++i) {
		const statusbar_rect &r = rects[i];
		if (x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h)
			return i;
	}
	return -1;
}
bool statusbar_point(float window_x, float window_y, int window_w, int window_h,
	int output_w, int output_h, const statusbar_rect &bar, float *x, float *y)
{
	if (window_w <= 0 || window_h <= 0 || output_w <= 0 || output_h <= 0 ||
		window_x < 0 || window_y < 0 || window_x >= window_w || window_y >= window_h)
		return false;
	float px = window_x * output_w / window_w;
	float py = window_y * output_h / window_h;
	if (statusbar_hit(&bar, 1, px, py) < 0)
		return false;
	*x = px - bar.x;
	*y = py - bar.y;
	return true;
}
int statusbar_led_state(const statusbar_state &state, int led)
{
	const gui_info &g = state.gui;
	if (led >= LED_DF0 && led <= LED_DF3)
		return g.drives[led - LED_DF0].drive_motor;
	switch (led) {
	case LED_POWER: return g.powerled;
	case LED_CAPS: return g.capslock;
	case LED_HD: return g.hd;
	case LED_CD: return g.cd;
	case LED_NET: return g.net;
	case LED_MD: return g.md;
	case LED_SND: return g.sndbuf_avail ? (g.sndbuf_status > 1 || g.sndbuf_status < 0 ? 0 : 1) : -1;
	default: return 1;
	}
}
bool statusbar_format(const statusbar_state &state, int led, int on, statusbar_item *item)
{
	memset(item, 0, sizeof(*item));
	TCHAR *ptr = item->text;
	TCHAR *tt = NULL;
	TCHAR *p;
	int pos = statusbar_position(led), j;
	int writing = 0, playing = 0, active2 = 0;
	int writeprotected = 0, center = 0;
	if (led >= LED_DF0 && led <= LED_DF3) {
		if (state.gui.drives[led - LED_DF0].drive_disabled)
			_tcscpy (ptr, _T(""));
		else
			_stprintf (ptr , _T("%02d"), state.gui.drives[led - LED_DF0].drive_track);
		const TCHAR *filename = state.gui.drives[led - LED_DF0].df;
		j = _tcslen(filename) - 1;
		if (j < 0)
			j = 0;
		while (j > 0) {
			if (filename[j - 1] == '\\' || filename[j - 1] == '/')
				break;
			j--;
		}
		tt = item->tooltip;
		tt[0] = 0;
		if (_tcslen(filename + j) > 0)
			_stprintf (tt, _T("%s [CRC=%08X]"), filename + j, state.gui.drives[led - LED_DF0].crc32);
		center = 1;
		if (state.gui.drives[led - LED_DF0].drive_writing)
			writing = 1;
		if (state.gui.drives[led - LED_DF0].floppy_protected)
			writeprotected = 1;
	} else if (led == LED_POWER) {
		ptr = _tcscpy(ptr, _T("Power"));
		center = 1;
	} else if (led == LED_CAPS) {
		ptr = _tcscpy(ptr, _T("Caps"));
		center = 1;
	} else if (led == LED_HD) {
		ptr = _tcscpy(ptr, _T("HD"));
		center = 1;
		if (on > 1)
			writing = 1;
	} else if (led == LED_CD) {
		ptr = _tcscpy(ptr, _T("CD"));
		center = 1;
		if (on >= 0) {
			if (on & LED_CD_AUDIO)
				playing = 1;
			else if (on & LED_CD_ACTIVE2)
				active2 = 1;
			on &= 1;
		}
	} else if (led == LED_NET) {
		ptr = _tcscpy(ptr, _T("N"));
		center = 1;
		if (on > 1)
			writing = 1;
	} else if (led == LED_LINES) {
		if (state.gui.fps_color < 2) {
			_stprintf(ptr, _T("%3d%c"), state.gui.lines, state.gui.lace ? 'i' : 'p');
		} else {
			_tcscpy(ptr, _T("----"));
		}
		on = 1;
	} else if (led == LED_FPS) {
		float fps = state.gui.fps / 10.0f;
		if (fps > 9999.9)
			fps = 9999.9;
		const TCHAR *rec = state.recording ? _T("R") : _T("");
		if (state.gui.fps_color == 2) {
			if (state.rtg)
				_stprintf(ptr, _T("%.1f%s [NS]"), state.refresh_rate, rec);
			else
				_stprintf(ptr, _T("No Sync%s"), rec);
		} else if (state.gui.fps_color == 3) {
			if (state.rtg)
				_stprintf(ptr, _T("%.1f%s [---]"), state.refresh_rate, rec);
			else
				_stprintf(ptr, _T("FPS: ---%s"), rec);
		} else if (fps < 1000) {
			if (state.rtg)
				_stprintf (ptr, _T("%.1f%s [%.1f]"), state.refresh_rate, rec, fps);
			else
				_stprintf (ptr, _T("FPS: %.1f%s"), fps, rec);
		} else {
			if (state.rtg)
				_stprintf(ptr, _T("%.0f%s [%.0f]"), state.refresh_rate, rec, fps);
			else
				_stprintf(ptr, _T("FPS: %.0f%s"), fps, rec);
		}
		if (state.gui.cpu_halted > 0) {
			_stprintf (ptr, _T("HALT%d%s"), state.gui.cpu_halted, rec);
			center = 1;
		}
		if (state.paused) {
			_tcscpy (ptr, _T("PAUSED"));
			center = 1;
		}
		on = 1;
	} else if (led == LED_CPU) {
		if (state.paused)
			on = 0;
		else
			on = 1;
		bool m68klabelchange = false;
		const TCHAR *m68label = _T("CPU");
		ptr[0] = 0;
		p = ptr;
		if (state.ppc) {
			_tcscat(ptr, _T("PPC: "));
			if (state.ppc_active)
				_tcscat(ptr, _T("RUN"));
			else
				_tcscat(ptr, _T("STOP"));
			_tcscat(ptr, _T(" "));
			p = ptr + _tcslen(ptr);
			m68label = _T("68k");
			m68klabelchange = true;
		}
		int x86 = state.x86;
		if (x86 > 0) {
			_tcscat(ptr, _T("x86: "));
			if (x86 == 1)
				_tcscat(ptr, _T("RUN"));
			else
				_tcscat(ptr, _T("STOP"));
			_tcscat(ptr, _T(" "));
			p = ptr + _tcslen(ptr);
			m68label = _T("68k");
			m68klabelchange = true;
		}
		if (state.gui.cpu_halted < 0 || state.gui.cpu_stopped) {
			if (!m68klabelchange)
				_tcscpy(p, _T("CPU:STOP"));
			else
				_tcscat(p, _T("68k: STOP"));
		} else {
			_stprintf(p, _T("%s: %.0f%%"), m68label, (double)((state.gui.idle) / 10.0));
		}
	} else if (led == LED_SND && state.gui.sndbuf_avail) {
		if (state.gui.sndbuf_status < 3 && !state.paused && !state.sound_paused) {
			_stprintf (ptr, _T("SND: %+.0f%%"), (double)((state.gui.sndbuf) / 10.0));
		} else {
			_tcscpy (ptr, _T("SND: -"));
			center = 1;
			on = 0;
		}
	} else if (led == LED_MD) {
		ptr = _tcscpy(ptr, _T("NV"));
	}

	if (on < 0)
		return false;

	if (pos < 0)
		return false;
	item->flags = (center ? STATUSBAR_CENTER : 0) | (on ? STATUSBAR_ON : 0) |
		(writing ? STATUSBAR_WRITING : 0) | (playing ? STATUSBAR_PLAYING : 0) |
		(active2 ? STATUSBAR_ACTIVE2 : 0) | (writeprotected ? STATUSBAR_PROTECTED : 0);
	return true;
}
