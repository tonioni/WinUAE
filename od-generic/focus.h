#ifndef OD_GENERIC_FOCUS_H
#define OD_GENERIC_FOCUS_H

/* Pause, mute and priority decisions when the emulator window gains or loses
 * focus, is minimized, or captures or releases the mouse. They act on the
 * active_*, inactive_* and iconified_* prefs. Shared by all hosts. */

/* What focus handling has paused: 0 nothing, 1 emulation, -1 only sound. */
extern int sound_closed;

/* The window got focus. minimized is true while it is still minimized. */
void focus_activated(bool minimized, bool captured);
/* The window lost focus or was minimized. */
void focus_deactivated(bool minimized, bool captured);
/* The mouse was captured or released while the window has focus. */
void focus_mouse_captured(void);
void focus_mouse_released(void);
/* The user captured or released the mouse explicitly. */
void focus_capture_enabled(void);
void focus_capture_disabled(void);

/* Host functions used by od-generic/focus.cpp. */

/* Pause or resume emulation. A pause at a higher priority wins over a lower
 * one, and resume only undoes a pause of the same or lower priority. */
bool setpaused(int priority);
bool resumepaused(int priority);
/* Pause or resume sound output only. */
void setsoundpaused(void);
void resumesoundpaused(void);
/* Apply a process priority (an index into the host's priority table). */
void host_priority_set(int index);

#endif /* OD_GENERIC_FOCUS_H */
