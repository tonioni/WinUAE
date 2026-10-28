// Pause, mute and priority handling for window focus, minimize and mouse
// capture changes. Host independent; split out of od-win32/win32.cpp.

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "inputdevice.h"
#include "uae.h"
#include "od-generic/focus.h"

int sound_closed;

void focus_activated(bool minimized, bool captured)
{
	int pri = currprefs.win32_inactive_priority;
	if (!minimized) {
		pri = currprefs.win32_active_capture_priority;
	}
	host_priority_set(pri);

	if (sound_closed) {
		if (sound_closed < 0) {
			resumesoundpaused();
		} else {
			if (currprefs.win32_active_nocapture_pause) {
				if (captured) {
					resumepaused(2);
				}
			} else if (currprefs.win32_iconified_pause && !currprefs.win32_inactive_pause) {
				resumepaused(1);
			} else if (currprefs.win32_inactive_pause) {
				resumepaused(2);
			}
		}
		sound_closed = 0;
	}
}

void focus_deactivated(bool minimized, bool captured)
{
	int pri = currprefs.win32_inactive_priority;
	if (!quit_program) {
		if (minimized) {
			pri = currprefs.win32_iconified_priority;
			if (currprefs.win32_iconified_pause) {
				inputdevice_unacquire();
				setpaused(1);
				sound_closed = 1;
			} else if (currprefs.win32_iconified_nosound) {
				inputdevice_unacquire(currprefs.win32_iconified_input);
				setsoundpaused();
				sound_closed = -1;
			} else {
				inputdevice_unacquire(currprefs.win32_iconified_input);
			}
		} else if (captured) {
			inputdevice_unacquire();
			if (currprefs.win32_active_nocapture_pause) {
				setpaused(2);
				sound_closed = 1;
			} else if (currprefs.win32_active_nocapture_nosound) {
				setsoundpaused();
				sound_closed = -1;
			}
		} else {
			if (currprefs.win32_inactive_pause) {
				inputdevice_unacquire();
				setpaused(2);
				sound_closed = 1;
			} else if (currprefs.win32_inactive_nosound) {
				inputdevice_unacquire(currprefs.win32_inactive_input);
				setsoundpaused();
				sound_closed = -1;
			} else {
				inputdevice_unacquire(currprefs.win32_inactive_input);
			}
		}
	} else {
		inputdevice_unacquire();
	}
	host_priority_set(pri);
}

void focus_mouse_captured(void)
{
	host_priority_set(currprefs.win32_active_capture_priority);
	if (currprefs.win32_active_nocapture_pause) {
		resumepaused(2);
	} else if (currprefs.win32_active_nocapture_nosound && sound_closed < 0) {
		resumesoundpaused();
	}
}

void focus_mouse_released(void)
{
	if (currprefs.win32_active_nocapture_pause) {
		setpaused(2);
	} else if (currprefs.win32_active_nocapture_nosound) {
		setsoundpaused();
		sound_closed = -1;
	}
}

void focus_capture_enabled(void)
{
	if (sound_closed < 0) {
		resumesoundpaused();
		sound_closed = 0;
	}
	if (currprefs.win32_inactive_pause || currprefs.win32_active_nocapture_pause) {
		resumepaused(2);
	}
}

void focus_capture_disabled(void)
{
	if (currprefs.win32_active_nocapture_pause && sound_closed == 0) {
		setpaused(2);
		sound_closed = 1;
	} else if (currprefs.win32_active_nocapture_nosound && sound_closed == 0) {
		setsoundpaused();
		sound_closed = -1;
	}
}
