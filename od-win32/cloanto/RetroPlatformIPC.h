/*****************************************************************************
 Name    : RetroPlatformIPC.h
 Project : RetroPlatform Player
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2019 Cloanto Corporation - All rights reserved. This
         : file is multi-licensed under the terms of the Mozilla Public License
         : version 2.0 as published by Mozilla Corporation and the GNU General
         : Public License, version 2 or later, as published by the Free
         : Software Foundation.
 Authors : os, m
 Created : 2007-08-27 13:55:49
 Updated : 2019-01-27 10:47:18
 Comment : RetroPlatform Player interprocess communication include file
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORMIPC_H__
#define __CLOANTO_RETROPLATFORMIPC_H__

#include <windows.h>

#define RETROPLATFORM_API_VER       "7.6"
#define RETROPLATFORM_API_VER_MAJOR  7
#define RETROPLATFORM_API_VER_MINOR  6

#define RPIPC_HostWndClass   "RetroPlatformHost%s"
#define RPIPC_GuestWndClass  "RetroPlatformGuest%d"

// Legacy Compatibility
#define RP_NO_LEGACY	// We don't need legacy #defines


// ****************************************************************************
//  Guest-to-Host Messages
// ****************************************************************************

#define RP_IPC_TO_HOST_PRIVATE_REGISTER     (WM_APP + 0)
#define RP_IPC_TO_HOST_FEATURES             (WM_APP + 1)
#define RP_IPC_TO_HOST_CLOSED               (WM_APP + 2)
#define RP_IPC_TO_HOST_ACTIVATED            (WM_APP + 3)
#define RP_IPC_TO_HOST_DEACTIVATED          (WM_APP + 4)
#define RP_IPC_TO_HOST_ENABLED              (WM_APP + 5)
#define RP_IPC_TO_HOST_DISABLED             (WM_APP + 6)
#define RP_IPC_TO_HOST_SCREENMODE           (WM_APP + 9)
#define RP_IPC_TO_HOST_POWERLED             (WM_APP + 10)
#define RP_IPC_TO_HOST_DEVICES              (WM_APP + 11)
#define RP_IPC_TO_HOST_DEVICEACTIVITY       (WM_APP + 12)
#define RP_IPC_TO_HOST_MOUSECAPTURE         (WM_APP + 13)
#define RP_IPC_TO_HOST_HOSTAPIVERSION       (WM_APP + 14)
#define RP_IPC_TO_HOST_PAUSE                (WM_APP + 15)
#define RP_IPC_TO_HOST_TURBO                (WM_APP + 17)
#define RP_IPC_TO_HOST_PING                 (WM_APP + 18)
#define RP_IPC_TO_HOST_VOLUME               (WM_APP + 19)
#define RP_IPC_TO_HOST_PARENT               (WM_APP + 21)
#define RP_IPC_TO_HOST_DEVICESEEK           (WM_APP + 22)
#define RP_IPC_TO_HOST_CLOSE                (WM_APP + 23)
#define RP_IPC_TO_HOST_DEVICEREADWRITE      (WM_APP + 24)
#define RP_IPC_TO_HOST_HOSTVERSION          (WM_APP + 25)
#define RP_IPC_TO_HOST_INPUTDEVICE          (WM_APP + 26) // introduced in RetroPlatform API 3.0
#define RP_IPC_TO_HOST_DEVICECONTENT	    (WM_APP + 27) // extended in RetroPlatform API 3.0
#define RP_IPC_TO_HOST_KEYBOARDLAYOUT		(WM_APP + 30) // introduced in RetroPlatform API 7.1
#define RP_IPC_TO_HOST_PRIVATE_SHAREDEVENT  (WM_APP + 31) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_HOST_PRIVATE_TYPECLIPDONE (WM_APP + 32) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_HOST_PRIVATE_KEYEVENT     (WM_APP + 33) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_HOST_PRIVATE_GUESTEVENT   (WM_APP + 34) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_HOST_PRIVATE_KEYREMINDER  (WM_APP + 35) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_HOST_RAWINPUT_EVENT       (WM_APP + 36) // introduced in RetroPlatform API 7.3
#define RP_IPC_TO_HOST_PRIVATE_CLOSEKBDWIN  (WM_APP + 37) // introduced in RetroPlatform API 7.4
#define RP_IPC_TO_HOST_MOUSEMOVE            (WM_APP + 38) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_HOST_MOUSEBUTTON          (WM_APP + 39) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_HOST_PRIVATE_MENUEVENT    (WM_APP + 40) // introduced in RetroPlatform API 7.5

// ****************************************************************************
//  Host-to-Guest Messages
// ****************************************************************************

#define RP_IPC_TO_GUEST_CLOSE                (WM_APP + 200)
#define RP_IPC_TO_GUEST_SCREENMODE           (WM_APP + 202)
#define RP_IPC_TO_GUEST_PAUSE                (WM_APP + 204)
#define RP_IPC_TO_GUEST_RESET                (WM_APP + 206)
#define RP_IPC_TO_GUEST_TURBO                (WM_APP + 207)
#define RP_IPC_TO_GUEST_PING                 (WM_APP + 208)
#define RP_IPC_TO_GUEST_VOLUME               (WM_APP + 209)
#define RP_IPC_TO_GUEST_EVENT                (WM_APP + 211)
#define RP_IPC_TO_GUEST_MOUSECAPTURE         (WM_APP + 212)
#define RP_IPC_TO_GUEST_SAVESTATE            (WM_APP + 213)
#define RP_IPC_TO_GUEST_LOADSTATE            (WM_APP + 214)
#define RP_IPC_TO_GUEST_FLUSH                (WM_APP + 215)
#define RP_IPC_TO_GUEST_DEVICEREADWRITE      (WM_APP + 216)
#define RP_IPC_TO_GUEST_QUERYSCREENMODE      (WM_APP + 217)
#define RP_IPC_TO_GUEST_GUESTAPIVERSION      (WM_APP + 218) // introduced in RetroPlatform API 3.0
#define RP_IPC_TO_GUEST_DEVICECONTENT        (WM_APP + 219) // extended in RetroPlatform API 3.0
#define RP_IPC_TO_GUEST_SCREENCAPTURE        (WM_APP + 220) // extended in RetroPlatform API 3.4
#define	RP_IPC_TO_GUEST_DEVICEACTIVITY       (WM_APP + 221) // introduced in RetroPlatform API 7.1
#define	RP_IPC_TO_GUEST_SHOWOPTIONS          (WM_APP + 222) // introduced in RetroPlatform API 7.1
#define	RP_IPC_TO_GUEST_PRIVATE_STARTUP      (WM_APP + 223) // introduced in RetroPlatform API 7.2
#define	RP_IPC_TO_GUEST_PRIVATE_TYPECLIP     (WM_APP + 224) // introduced in RetroPlatform API 7.2
#define	RP_IPC_TO_GUEST_PRIVATE_RELEASEKEYS  (WM_APP + 225) // introduced in RetroPlatform API 7.2
#define	RP_IPC_TO_GUEST_PRIVATE_CANESCAPE    (WM_APP + 226) // introduced in RetroPlatform API 7.2
#define	RP_IPC_TO_GUEST_PRIVATE_LOGGING      (WM_APP + 227) // introduced in RetroPlatform API 7.2
#define	RP_IPC_TO_GUEST_PRIVATE_INPUTDEVICES (WM_APP + 228) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_GUEST_PRIVATE_KEYREMINDER  (WM_APP + 229) // introduced in RetroPlatform API 7.2
#define RP_IPC_TO_GUEST_PRIVATE_KEYBOARDWINDOW (WM_APP + 230) // introduced in RetroPlatform API 7.4
#define RP_IPC_TO_GUEST_SCREENOVERLAY        (WM_APP + 231) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_GUEST_MOVESCREENOVERLAY    (WM_APP + 232) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_GUEST_DELETESCREENOVERLAY  (WM_APP + 233) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_GUEST_SENDMOUSEEVENTS      (WM_APP + 234) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_GUEST_PRIVATE_MENUMODE     (WM_APP + 235) // introduced in RetroPlatform API 7.5
#define RP_IPC_TO_GUEST_PRIVATE_SETFRGWINDOW (WM_APP + 236) // introduced in RetroPlatform API 7.5
#define	RP_IPC_TO_GUEST_SHOWDEBUGGER         (WM_APP + 237) // introduced in RetroPlatform API 7.6

// ****************************************************************************
//  Message Data Structures and Defines
// ****************************************************************************

// Guest Features
#define RP_FEATURE_POWERLED        			0x00000001 // a power LED is emulated
#define RP_FEATURE_SCREEN1X       			0x00000002 // 1x mode is available (1 Amiga hires interlaced/RTG pixel = 1 host pixel)
#define RP_FEATURE_SCREEN2X        			0x00000004 // 2x mode is available
#define RP_FEATURE_SCREEN3X        			0x00000008 // 3x mode is available
#define RP_FEATURE_SCREEN4X        			0x00000010 // 4x mode is available
#define RP_FEATURE_FULLSCREEN      			0x00000020 // fullscreen display is available
#define RP_FEATURE_RESERVED   			    0x00000040 // unused - reserved for future use (to not cause side effects during screen capture changes in 201303 beta)
#define RP_FEATURE_PAUSE           			0x00000080 // pause functionality is available (see RP_IPC_TO_GUEST_PAUSE message)
#define RP_FEATURE_TURBO_CPU       			0x00000100 // turbo CPU functionality is available (see RP_IPC_TO_GUEST_TURBO message)
#define RP_FEATURE_VOLUME          			0x00000200 // volume adjustment is possible (see RP_IPC_TO_GUEST_VOLUME message)
#define RP_FEATURE_STATE           			0x00000400 // loading and saving of emulation state is supported (see RP_IPC_TO_GUEST_SAVESTATE/RP_IPC_TO_GUEST_LOADSTATE message)
#define RP_FEATURE_SCANLINES       			0x00000800 // scan lines video effect is available
#define RP_FEATURE_DEVICEREADWRITE 			0x00001000 // device read/write can be set at runtime on floppy and hard disks
#define RP_FEATURE_SCALING_SUBPIXEL 		0x00002000 // supports sub-pixel scaling of windowed and fullscreen modes (i.e. not just integer multipliers like 1X, 2X, etc., but stretch to fill any desired pixel size)
#define RP_FEATURE_SCALING_STRETCH          0x00004000 // supports "stretch to fill" (without preserving original ratio) in fullscreen mode or with lTargetWidth and lTargetHeight set
#define RP_FEATURE_INPUTDEVICE_MOUSE	    0x00008000 // supports emulation of mouse
#define RP_FEATURE_INPUTDEVICE_JOYSTICK	    0x00010000 // supports emulation of one/two-button joystick
#define RP_FEATURE_INPUTDEVICE_GAMEPAD	    0x00020000 // supports emulation of multi-button joystick (if 3+ buttons available)
#define RP_FEATURE_INPUTDEVICE_JOYPAD	    0x00040000 // supports emulation of amiga cd32 joypad
#define RP_FEATURE_INPUTDEVICE_PADDLE	    0x00080000 // supports emulation of analog paddle
#define RP_FEATURE_INPUTDEVICE_ANALOGSTICK	0x00100000 // supports emulation of analog joystick
#define RP_FEATURE_INPUTDEVICE_LIGHTPEN	    0x00200000 // supports emulation of light pen
#define RP_FEATURE_INPUTDEVICE_TABLET	    0x00400000 // supports emulation of pen tablet
#define RP_FEATURE_TURBO_FLOPPY    			0x00800000 // turbo floppy functionality is available (see RP_IPC_TO_GUEST_TURBO message)
#define RP_FEATURE_TURBO_TAPE      			0x01000000 // turbo tape functionality is available (see RP_IPC_TO_GUEST_TURBO message)
#define RP_FEATURE_MEMORY_BASIC   		    0x02000000 // Memory I/O basic features: Read, Write
#define RP_FEATURE_MEMORY_ADVANCED		    0x04000000 // Memory I/O advanced features: Watch, Find, Alert, Freeze, Lock, Unlock, Off (must set both flags if full set is supported!)
#define RP_FEATURE_SCREENCAPTURE   			0x08000000 // new screen capture functionality is available (see RP_IPC_TO_GUEST_SCREENCAPTURE message)
#define RP_FEATURE_RAWINPUT_EVENT			0x10000000 // RawInput mouse buttons events are forwarded via RP_IPC_TO_HOST_RAWINPUT_EVENT messages
#define RP_FEATURE_SCREENOVERLAY			0x20000000 // supports screen overlays

typedef struct RPScreenMode
{
	DWORD dwScreenMode; // RP_SCREENMODE_* values and flags
	LONG lClipLeft;     // in guest pixel units (Amiga: Super Hires or RTG); -1 = ignore (0 is a valid value); guest should also ignore this when receiving data if RP_CLIPFLAGS_AUTOCLIP or RP_CLIPFLAGS_NOCLIP; see http://www.retroplatform.com/kb/19-115
	LONG lClipTop;      // in guest pixel units (Amiga: interlaced or RTG); -1 = ignore (0 is a valid value); guest should also ignore this when receiving data if RP_CLIPFLAGS_AUTOCLIP or RP_CLIPFLAGS_NOCLIP
	LONG lClipWidth;    // in guest pixel units (Amiga: Super Hires or RTG); -1 = ignore; guest should also ignore this when receiving data if RP_CLIPFLAGS_AUTOCLIP or RP_CLIPFLAGS_NOCLIP
	LONG lClipHeight;   // in guest pixel units (Amiga: interlaced or RTG); -1 = ignore; guest should also ignore this when receiving data if RP_CLIPFLAGS_AUTOCLIP or RP_CLIPFLAGS_NOCLIP
	HWND hGuestWindow;  // only valid for RP_IPC_TO_HOST_SCREENMODE
	DWORD dwClipFlags;	// clip flags (or 0)
	LONG lTargetWidth;  // in exact host pixels; if set, must also set lTargetHeight; ignored unless RP_SCREENMODE_SCALE_TARGET is set (resulting size is result of clipping and scaling); RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH are taken into account
	LONG lTargetHeight; // in exact host pixels, used with lTargetWidth
} RPSCREENMODE;

// See below for more notes about lClipLeft, lClipTop, lClipWidth, lClipHeight, etc.


// Scaling Factor (for Window and Fullscreen)
#define RP_SCREENMODE_SCALE_1X      		0x00000000 // 1x window or fullscreen mode ("CGA mode")
#define RP_SCREENMODE_SCALE_2X      		0x00000001 // 2x window or fullscreen mode ("VGA mode")
#define RP_SCREENMODE_SCALE_3X      		0x00000002 // 3x window or fullscreen mode ("triple CGA mode")
#define RP_SCREENMODE_SCALE_4X      		0x00000003 // 4x window or fullscreen mode ("double VGA mode")
#define RP_SCREENMODE_SCALE_TARGET     		0x000000FE // scale to maximum within lTargetWidth/lTargetHeight (default: maximum possible integer multiplication, no subpixel stretching, preserve ratio; may change to non-integer mode depending on RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH)
#define RP_SCREENMODE_SCALE_MAX        		0x000000FF // scale to auto-determined maximum size (default: maximum possible nX integer multiplication, no subpixel stretching, preserve ratio; may change to non-integer mode depending on RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH); must be set in fullscreen (shared or exclusive) modes
#define RP_SCREENMODE_SCALEMASK      		0x000000FF
#define RP_SCREENMODE_SCALE(m)       		((m) & RP_SCREENMODE_SCALEMASK) // returns the #X mode

// Display: Window vs. Fullscreen (with Multi-Monitor Support)
#define RP_SCREENMODE_DISPLAY_WINDOW		0x00000000 // if no RP_SCREENMODE_DISPLAY_FULLSCREEN_x flags are set, it means normal window with borders and controls
#define RP_SCREENMODE_DISPLAY_FULLSCREEN_1	0x00000100 // fullscreen on primary (default) display; requires RP_SCREENMODE_SCALE_MAX and RP_SCREENMODE_FULLSCREEN_EXCLUSIVE or RP_SCREENMODE_FULLSCREEN_SHARED
#define RP_SCREENMODE_DISPLAY_FULLSCREEN_2	0x00000200 // fullscreen on secondary display (fallback to 1 if unavailable); requires RP_SCREENMODE_SCALE_MAX and RP_SCREENMODE_FULLSCREEN_EXCLUSIVE or RP_SCREENMODE_FULLSCREEN_SHARED
#define RP_SCREENMODE_DISPLAYMASK			0x0000FF00
#define RP_SCREENMODE_DISPLAY(m)			(((m) & RP_SCREENMODE_DISPLAYMASK) >> 8) // returns the display number (1-255) or 0 if full screen is not active

// Fullscreen Type: Hardware (Exclusive) vs. Software (Shared) Fullscreen (both are only used with RP_SCREENMODE_DISPLAY_FULLSCREEN_x)
#define RP_SCREENMODE_FULLSCREEN_EXCLUSIVE	0x00000000 // use display card video modes (aka "real fullscreen", may change resolution/frequency); must have RP_SCREENMODE_DISPLAY_FULLSCREEN_n and RP_SCREENMODE_SCALE_MAX set
#define RP_SCREENMODE_FULLSCREEN_SHARED		0x00010000 // use "full window" (desktop size) when in fullscreen (no gfx card fullscreen); must have RP_SCREENMODE_DISPLAY_FULLSCREEN_n and RP_SCREENMODE_SCALE_MAX set
#define RP_SCREENMODE_FULLSCREENMASK       	0x00010000
#define RP_SCREENMODE_FULLSCREEN(m)       	((m) & RP_SCREENMODE_FULLSCREENMASK) // returns RP_SCREENMODE_FULLSCREEN_EXCLUSIVE or RP_SCREENMODE_FULLSCREEN_SHARED

// Fullscreen: TV Modes (only apply if RP_SCREENMODE_DISPLAY_FULLSCREEN_x and RP_SCREENMODE_FULLSCREEN_EXCLUSIVE)
#define RP_SCREENMODE_USETVM_NEVER  		0x00000000 // never use TV modes
#define RP_SCREENMODE_USETVM_ALWAYS 		0x00020000 // always use TV modes
#define RP_SCREENMODE_USETVM_AUTO   		0x00040000 // use all available modes
#define RP_SCREENMODE_USETVMMASK    		0x00060000
#define RP_SCREENMODE_USETVM(m)     		((m) & RP_SCREENMODE_USETVMMASK) // returns the RP_SCREENMODE_USETVM_* value

// Additional Options for Window and Fullscreen
#define RP_SCREENMODE_SCANLINES     		0x00080000 // show video scan lines
#define RP_SCREENMODE_SCALING_SUBPIXEL 		0x00100000 // use sub-pixel (non-integer) scaling in RP_SCREENMODE_SCALE_TARGET or RP_SCREENMODE_SCALE_MAX modes; if not set, up to four black bars may be added; if set, up to two black bars may be added
#define RP_SCREENMODE_SCALING_STRETCH  		0x00200000 // "stretch to fill" (do not preserve original ratio) in RP_SCREENMODE_SCALE_TARGET or RP_SCREENMODE_SCALE_MAX modes; if set, no black bars are added

// Clip Flags (used only from host to guest, never from guest to host)
#define RP_CLIPFLAGS_AUTOCLIP				0x00000001 // ignore all 4 Clip values (same as all values = -1) and use "smart" offset and size
#define RP_CLIPFLAGS_NOCLIP					0x00000002 // ignore all 4 Clip values (same as all values = -1) and use 0:0 offset and maximum possible size (probably ugly, but good for adjusting clip area manually)

//
// Integer vs. subpixel scaling, and stretching with or without original ratio
//
// By default, the guest is only expected to be able to scale (resize, units being guest to host pixels) the emulated screen by an integer number of times, e.g. 1X, 2X, 3X, etc., as indicated in RP_FEATURE_SCREEN...
// This means that when going to fullscreen mode (RP_SCREENMODE_SCALE_MAX) or to a window mode with lTargetWidth and lTargetHeight set (RP_SCREENMODE_SCALE_TARGET), the content will be an integer number of times the original number of pixels, and surrounded by black bars if necessary. Integer scaling is thus the default behavior, and it can be modified by setting RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH.
//
// In a known-size target (e.g. fullscreen), unless RP_SCREENMODE_SCALING_SUBPIXEL or RP_SCREENMODE_SCALING_STRETCH are set, there may be up to four black bars to fill any unused space.
//
// RP_SCREENMODE_SCALING_SUBPIXEL (used only if RP_FEATURE_SCALING_SUBPIXEL was set) allows for subpixel resize scenarios beyond integer 1X, 2X, etc. This is both in fullscreen and in window mode.
//
// In a known-size target (e.g. fullscreen), RP_SCREENMODE_SCALING_SUBPIXEL allows for "stretch to fill" with black areas only on two sides out of four (e.g. 4:3 displayed on 16:9 will fill to the top and bottom, and have bars only on the left and right).
//
// RP_SCREENMODE_SCALING_STRETCH (used only if RP_FEATURE_SCALING_STRETCH was set) allows to break (not respect) the original screen ratio and allows for maximum "stretch to fill" with no black areas. It is used with RP_SCREENMODE_SCALING_SUBPIXEL.
//
// In theory RP_SCREENMODE_SCALING_STRETCH could be used alone (without RP_SCREENMODE_SCALING_SUBPIXEL), but it probably doesn't make practical sense. In any case, the theoretical result would be scaling with different integer values for X and Y.
//
//
// Typical scenarios:
//
// - Window mode without lTargetWidth and lTargetHeight set (user clicks 1X, 2X, etc. to resize)
// - Window mode with lTargetWidth and lTargetHeight set (user resizes via window resize corner)
// - Fullscreen mode
// - Real-time clip changes (users clicks a control and uses cursor keys to visually adjust clip)
//
//
// Full-window Examples
//
// Example 1: No RP_SCREENMODE_SCALING_SUBPIXEL, no RP_SCREENMODE_SCALING_STRETCH: maximum integer scaling (can have black bars on up to four sides), e.g. 1X, 2X, 3X, etc.
//
// Example 2: RP_SCREENMODE_SCALING_SUBPIXEL, no RP_SCREENMODE_SCALING_STRETCH: maximum "soft" scaling keeping ratio (can have black bars on two sides)
//
// Example 3: RP_SCREENMODE_SCALING_SUBPIXEL combined with RP_SCREENMODE_SCALING_STRETCH: maximum "stretch to fill" with no black areas
//
// There is a (theoretical) fourth case (RP_SCREENMODE_SCALING_STRETCH without RP_SCREENMODE_SCALING_SUBPIXEL), which could result in different integer multiplications along each axis. The player will not request such a mode.
//
//
// Clipping Examples
//
// In the following examples it is assumed that RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH are cleared unless specified otherwise.
//
// Example 1: An Amiga game with known clip offset/size will have lClipLeft/Top/Width/Height set, and no RP_CLIPFLAGS.
// In windowed mode, the guest (e.g. WinUAE) will take the net clipped region and apply RP_SCREENMODE_SCALE_nX scaling to that.
// In RP_SCREENMODE_FULLSCREEN_SHARED mode, the net clipped region will be scaled to the maximum possible (integer scaling factor or otherwise, if RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH are set), centered and padded by black bars if necessary (unless RP_SCREENMODE_SCALING_SUBPIXEL and RP_SCREENMODE_SCALING_STRETCH are set).
// In RP_SCREENMODE_FULLSCREEN_EXCLUSIVE mode ("hardware fullscreen"), the net clipped region will be centered in the smallest possible compatible hardware mode (in consideration of RP_SCREENMODE_USETVM) and padded by a black border.
// If an Amiga application sets a different Amiga chipset screen mode, the "container" window size will remain unchanged (because it is still constrained by the clipping values).
// If an Amiga application sets a different RTG screen mode, the "container" window size will reflect the new RTG size (instead of the Amiga clip size) and apply RP_SCREENMODE_SCALE_nX.
//
// Example 2: An unknown Amiga application or one that has no known clip offset/size will start with RP_CLIPFLAGS_AUTOCLIP.
// The guest (e.g. WinUAE) will apply whatever logic it can to minimize the visible overscan region.
// The guest will send to the host the actual RPScreenMode data (with clipping details, rather than with -1) with the offset/size details that were applied.
// In windowed mode, RP_SCREENMODE_SCALE_nX scaling is applied like in the previous example.
// RP_SCREENMODE_FULLSCREEN_SHARED and RP_SCREENMODE_FULLSCREEN_EXCLUSIVE modes behave like in the previous example (scaling, etc.)
// If an Amiga application sets a different Amiga or RTG chipset screen mode, the "container" window size may change (because it is not constrained by any host-set clipping values), in consideration of RP_SCREENMODE_SCALE_nX.
//
//
// Notes
//
// For information on display clipping values, see http://www.retroplatform.com/kb/19-115
//
// If the user wants to adjust clipping, or for automated grabs and calculations, it is possible to set RP_CLIPFLAGS_NOCLIP, which will widen the window to the maximum (within lTargetWidth+lTargetHeight/fullscreen constraints).
//
// Both in fullscreen and in window mode, whenever the guest sets or changes the net window size or the scaling factor (initially, or due to a command it receives, or due to Amiga-sourced changes), it sends an RPScreenMode update to the host. This is useful also in fullscreen mode, as it allows to display the appropriate scaling options to return in window mode.

//
// In window mode, if no lTargetWidth and lTargetHeight are set, when the host asks for a change in clipping (net content size), the host window size will be adjusted taking into account the current integer multiplication factor.
//
// In window mode, after a change of clipping or size, the player may have to reset the visual hilight of the 1X, 2X etc. buttons according to new RP_IPC_TO_HOST_SCREENMODE data (setting a hilight if the correct scaling button is present in the user interface, or removing all hilights if the corresponding button is missing).
//
// If the host finds an unexpected "-1" or an out-of-bounds value in the lClipLeft/Top/Width/Height
// values when it needs to display a clipping outline (for editing)
// to the user, it can solve the problem (rather than making the editing action unavailable) in
// one of two ways:
//
// - Display a "widest possible" outline (as if there was no clipping) and let the user narrow
//   that manually
//
// - Apply its own auto-clipping logic (as already used when images are dragged and dropped to
//   add preview thumbnails) to set an initial outline, and let the user change it from there


// RPInputDeviceDescription (used by initial host device enumeration via RP_IPC_TO_HOST_INPUTDEVICE, right after RP_IPC_TO_HOST_FEATURES and before RP_IPC_TO_HOST_SCREENMODE)

typedef struct RPInputDeviceDescription
{
    DWORD dwHostInputType;              // host-side input device type (RP_HOSTINPUT_MOUSE, RP_HOSTINPUT_JOYSTICK, etc.)
	WCHAR szHostInputID[260];           // host device "ProductGUID InstanceGUID", GUID, etc. (can be any format, as long as the string is unique across all devices; keyboard layout strings must not contain spaces)
	WCHAR szHostInputName[260];         // host device product description string ("5-Axis,12-Button with POV", "HID keyboard device", etc.) as listed by Windows; identical devices will result in identical strings (it is up to the host to add " (2)" etc. to the display names)
	DWORD dwHostInputVendorID;          // host device Vendor ID (identification as issued by usb.org and found in USB and DirectInput device descriptors), or 0 if not available
	DWORD dwHostInputProductID;         // host device Product ID (identification as assigned by manufacturer and used in USB and DirectInput device descriptors), or 0 if not available
	DWORD dwInputDeviceFeatures;        // supported guest-side usage bitfield (uses feature flags, e.g. RP_FEATURE_INPUTDEVICE_MOUSE | RP_FEATURE_INPUTDEVICE_JOYSTICK, etc.); example: PC analog joystick can be used as Amiga mouse and joystick
	DWORD dwFlags;	                    // flags (or 0); example: RP_HOSTINPUTFLAGS_MOUSE_SMART, etc.
} RPINPUTDEVICEDESCRIPTION;


// RPDeviceContent (used by RP_IPC_TO_HOST_DEVICECONTENT and RP_IPC_TO_GUEST_DEVICECONTENT)

typedef struct RPDeviceContent
{
	BYTE btDeviceCategory;              // RP_DEVICECATEGORY_* value
	BYTE btDeviceNumber;                // device number (range 0..31), e.g. Amiga floppy drive unit 0, C64 disk unit 8 or 9, etc.
    DWORD dwInputDevice;                // (guest-side) input device type (RP_INPUTDEVICE_MOUSE, RP_INPUTDEVICE_JOYSTICK, etc.); currently set to 0 if not RP_DEVICE_INPUTPORT
	DWORD dwFlags;	                    // flags (or 0); e.g. see RP_DEVICEFLAGS_MOUSE_ (for "mouse hack"), RP_DEVICEFLAGS_RW_ (for read/write status)
	WCHAR szContent[260];               // if RP_DEVICECATEGORY_INPUTPORT (mice only), then host device ID, otherwise full path and name of the media image file to load, if file content (not used for input devices, which only use szHostInputID); szContent is ignored if btDeviceCategory == RP_DEVICECATEGORY_INPUTPORT and dwInputDevice == RP_INPUTDEVICE_EMPTY
} RPDEVICECONTENT;


// Device Categories
#define RP_DEVICECATEGORY_FLOPPY         0 // floppy disk drive
#define RP_DEVICECATEGORY_HD             1 // hard disk drive
#define RP_DEVICECATEGORY_CD             2 // CD/DVD drive
#define RP_DEVICECATEGORY_NET            3 // network card
#define RP_DEVICECATEGORY_TAPE           4 // cassette tape drive
#define RP_DEVICECATEGORY_CARTRIDGE      5 // expansion cartridge
#define RP_DEVICECATEGORY_INPUTPORT      6 // input port (hosts an INPUTDEVICE: mouse, joystick, etc.)
#define RP_DEVICECATEGORY_KEYBOARD       7 // keyboard
#define RP_DEVICECATEGORY_MULTITAPPORT   8 // multitap port (e.g. input port on Amiga parallel port joystick adapter)
#define RP_DEVICECATEGORY_COUNT          9 // total number of device categories

#define RP_ALL_DEVICES             32 // constant for the RP_IPC_TO_HOST_DEVICEACTIVITY message (to turn on/off all LEDs for a device category)


// Host Input Device Types (used to enumerate host devices)
#define RP_HOSTINPUT_MOUSE          0 // Mouse/trackball (supports relative moves)
#define RP_HOSTINPUT_TABLET         1 // Pen tablet (no relative moves, only absolute positions)
#define RP_HOSTINPUT_JOYSTICK       2 // [LEGACY] PC joystick, gamepad, trackball, etc.
#define RP_HOSTINPUT_KEYJOY_MAP1    3 // [LEGACY] Keyboard Layout 1; Amiga/C64: Keyboard Layout A for WinUAE/VICE (8, 2, 4, 6 on keypad, 0 to fire, etc.)
#define RP_HOSTINPUT_KEYJOY_MAP2    4 // [LEGACY] Keyboard Layout 2; Amiga/C64: Keyboard Layout B for WinUAE/VICE (cursor keys, right Control to fire, etc.)
#define RP_HOSTINPUT_KEYJOY_MAP3    5 // [LEGACY] Keyboard Layout 3; Amiga/C64: Keyboard Layout C for WinUAE/VICE (W, S, A, D keys, left Alt to fire, etc.)
#define RP_HOSTINPUT_ARCADE_LEFT    6 // [LEGACY] Left part of arcade dual joystick input device ("player 1")
#define RP_HOSTINPUT_ARCADE_RIGHT   7 // [LEGACY] Right part of arcade dual joystick input device ("player 2")
#define RP_HOSTINPUT_KEYBOARD       8 // [LEGACY] Keyboard Layout, using DirectInput keyboard scan codes (e.g. "KeyboardJoystick Left=0x4B Right=0x4D Up=0x48 Down=0x50 Fire=0x4C Autofire=0x38 Fire2=0x52 Rewind=0xB5 Play=0x37 FastForward=0x4A Green=0x47 Yellow=0x49 Red=0x4F Blue=0x51" set in szContent); introduced in RP API 3.3 to replace other keyboard layout modes
#define RP_HOSTINPUT_END            9 // "End of device enumeration" (dummy device used to terminate an input device set that began with the first input device)
#define RP_HOSTINPUT_COUNT         10 // total number of device types

// Host Input Device Flags
#define RP_HOSTINPUTFLAGS_MOUSE_RAW     0x00000000  // Individual raw mouse device with no acceleration (multi-mouse warning: RAW and SMART devices cannot be used simultaneously)
#define RP_HOSTINPUTFLAGS_MOUSE_SMART   0x00000001  // System pointer mouse with acceleration and aggregation of multiple devices
#define RP_HOSTINPUTFLAGS_MOUSEMASK     0x00000001
#define RP_HOSTINPUTFLAGS_MOUSE(m)      ((m) & RP_HOSTINPUTFLAGS_MOUSEMASK) // returns RP_HOSTINPUTFLAGS_MOUSE_RAW or RP_HOSTINPUTFLAGS_MOUSE_SMART


// Guest Input Device Types (used to describe what the host side device is mapped to on the guest side)
#define RP_INPUTDEVICE_EMPTY        0 // no device (empty port); can also be used from guest to host when something fails, to show to user that port is empty and requires new selection
#define RP_INPUTDEVICE_MOUSE	    1 // Mouse/trackball (supports relative moves); on Amiga: third button and wheel emulated, if available
#define RP_INPUTDEVICE_JOYSTICK	    2 // One/Two-button joystick
#define RP_INPUTDEVICE_GAMEPAD	    3 // Multi-button joystick (if 3+ buttons available); on Amiga: pull-up resistors emulated
#define RP_INPUTDEVICE_JOYPAD	    4 // Amiga: CD32 Joypad
#define RP_INPUTDEVICE_PADDLE	    5 // [CURRENTLY UNUSED] Analog paddle (e.g. as in VIC 20 470 kohm paddles); one paddle is mapped to one host-side device
#define RP_INPUTDEVICE_ANALOGSTICK	6 // [CURRENTLY UNUSED] Analog joystick; on Amiga: 2nd and 3rd button lines used as analog X/Y axes, digital directions used as buttons 1-4, plus original fire
#define RP_INPUTDEVICE_LIGHTPEN	    7 // [CURRENTLY UNUSED] Light pen
#define RP_INPUTDEVICE_TABLET	    8 // [CURRENTLY UNUSED] Pen tablet (no relative moves, only absolute positions); on Amiga: pressure support as per Electronic Arts Tablet.library
#define RP_INPUTDEVICE_COUNT        9 // total number of device types

// Device Read/Write status (used in RP_IPC_TO_HOST_DEVICECONTENT, RP_IPC_TO_GUEST_DEVICECONTENT; used for device categories RP_DEVICECATEGORY_FLOPPY, RP_DEVICECATEGORY_HD, RP_DEVICECATEGORY_TAPE, RP_DEVICECATEGORY_CARTRIDGE)
#define RP_DEVICEFLAGS_RW_READONLY          0x00000000  // the medium is write-protected
#define RP_DEVICEFLAGS_RW_READWRITE         0x00000001  // the medium is read/write
#define RP_DEVICEFLAGS_RWMASK               0x00000001
#define RP_DEVICEFLAGS_RW(m)       	        ((m) & RP_DEVICEFLAGS_RWMASK) // returns RP_DEVICEFLAGS_RW_READONLY or RP_DEVICEFLAGS_RW_READWRITE

// Mouse-like Device Types
#define RP_DEVICEFLAGS_MOUSE_FULL           0x00000000  // Full-featured X/Y device, supporting relative moves
#define RP_DEVICEFLAGS_MOUSE_ABSOLUTE       0x00000002  // Mouse features on guest side, but absolute-only device on host side (virtual/remote machine, limited tablet driver, etc.); WinUAE: "mousehack" mode
#define RP_DEVICEFLAGS_MOUSEMASK            0x00000002
#define RP_DEVICEFLAGS_MOUSE(m)       	    ((m) & RP_DEVICEFLAGS_MOUSEMASK) // returns RP_DEVICEFLAGS_MOUSE_FULL or RP_DEVICEFLAGS_MOUSE_ABSOLUTE

// Joystick status flags
#define RP_JOYSTICK_RIGHT    0x00000001 // right direction
#define RP_JOYSTICK_LEFT     0x00000002 // left direction
#define RP_JOYSTICK_DOWN     0x00000004 // down direction
#define RP_JOYSTICK_UP       0x00000008 // up direction
#define RP_JOYSTICK_BUTTON1  0x00000010 // button 1 - Fire 1 - CD32 Red
#define RP_JOYSTICK_BUTTON2  0x00000020 // button 2 - Fire 2 - CD32 Blue
#define RP_JOYSTICK_BUTTON3  0x00000040 // button 3 - Fire 3 - CD32 Yellow
#define RP_JOYSTICK_BUTTON4  0x00000080 // button 4 - Fire 4 - CD32 Green
#define RP_JOYSTICK_BUTTON5  0x00000100 // button 5 - CDTV/CD32 Play/Pause
#define RP_JOYSTICK_BUTTON6  0x00000200 // button 6 - CDTV/CD32 Reverse
#define RP_JOYSTICK_BUTTON7  0x00000400 // button 7 - CDTV/CD32 Forward

// Device Read/Write status (used in RP_IPC_TO_HOST_DEVICEREADWRITE, RP_IPC_TO_GUEST_DEVICEREADWRITE; used for device categories RP_DEVICECATEGORY_FLOPPY, RP_DEVICECATEGORY_HD, RP_DEVICECATEGORY_TAPE, RP_DEVICECATEGORY_CARTRIDGE)
#define RP_DEVICE_READONLY   0 // the medium is write-protected
#define RP_DEVICE_READWRITE  1 // the medium is read/write

// Turbo Mode Functionalities
#define RP_TURBO_CPU     0x00000001 // CPU
#define RP_TURBO_FLOPPY  0x00000002 // floppy disk drive
#define RP_TURBO_TAPE    0x00000004 // cassette tape drive

// Reset Type
#define RP_RESET_SOFT  0 // soft reset
#define RP_RESET_HARD  1 // hard reset

// RP_IPC_TO_HOST_MOUSECAPTURE/RP_IPC_TO_GUEST_MOUSECAPTURE
#define RP_MOUSECAPTURE_CAPTURED     0x00000001 // "trapped" (host system pointer becomes hidden)
#define RP_MOUSECAPTURE_INTEGRATED   0x00000002 // "force-push to untrap" aka "magic mouse" active

// RP_IPC_TO_GUEST_EVENT
//
// KEY_RAW_DOWN <x>: <x> is a numeric keycode (e.g. 0x4F or 79) that uniquely identifies the raw key on the guest system
// KEY_RAW_UP <x>: <x> is a numeric keycode (e.g. 0x4F or 79) that uniquely identifies the raw key on the guest system
//

// RP_IPC_TO_HOST_DEVICEACTIVITY
#define RP_DEVICEACTIVITY_GREEN    0x0000 // green led
#define RP_DEVICEACTIVITY_RED      0x0001 // red led
#define RP_DEVICEACTIVITY_READ     RP_DEVICEACTIVITY_GREEN // device activity is a read operation
#define RP_DEVICEACTIVITY_WRITE    RP_DEVICEACTIVITY_RED   // device activity is a write operation


// RPScreenCapture (used by RP_IPC_TO_GUEST_SCREENCAPTURE to request one bitmap or two simultaneous bitmaps)
// See RP_GUESTSCREENFLAGS_ for return code and flags

typedef struct RPScreenCapture
{
	DWORD dwFlags;	                    // currently set to 0
	WCHAR szScreenRaw[260];             // "\0" or full path and name (Unicode) of the BMP file to save - unfiltered, unscaled, unclipped bitmap (i.e. close to guest representation)
	WCHAR szScreenFiltered[260];        // "\0" or full path and name (Unicode) of the BMP file to save - filtered (e.g. scanline effects), scaled (e.g. line-doubled and 2X-multiplied), clipped (overscan cropped) bitmap (i.e. as displayed in host environment)
} RPSCREENCAPTURE;

//
// The return value of RP_IPC_TO_GUEST_SCREENCAPTURE serves to both indicate an error (if 0)
// and (if set to other RP_GUESTSCREENFLAGS_ values) to allow the guest to interpret and
// process the raw image (i.e. to scale and present appropriate PAL/NTSC options to the user).
//
// If the raw image is not the same as the Amiga bitmap, then out of necessity the flags
// describe the properties of the image that is sent to the host, not the Amiga mode. Otherwise
// the host cannot process the image and align it properly for clipping and non-raw overlay.
//
// I.e. if a PAL/NTSC image is set to RP_GUESTSCREENFLAGS_VERTICAL_NONINTERLACED, the host will
// typically double it vertically. If the image is already "deinterlaced" (doubled), the guest
// should set RP_GUESTSCREENFLAGS_VERTICAL_INTERLACED (even if the Amiga image is not
// interlaced), to avoid an additional doubling.
//
// Similarly, the RP_GUESTSCREENFLAGS_HORIZONTAL_xxx flag would normally describe the "real"
// Amiga mode, but if the guest can't send a bitmap that matches the original type then
// the flags should be adjusted accordingly. E.g. if the Amiga mode is Lores but the guest
// is sending a "raw" image where the pixels are doubled, then RP_GUESTSCREENFLAGS_HORIZONTAL_HIRES
// is expected. This is also the case if Super Hires pixels are halved.
//
// The top-left origin of the Raw bitmap should always be as if clipping values were set to the
// currently-agreed minimum. [this is a "bad" explanation, but functionally effective]
//
// Sample raw bitmap sizes for Amiga modes:
// - 752x484 if the flags indicate NTSC Hires/Interlaced
// - 752x576 if the flags indicate PAL Hires/Interlaced
// - Original RTG size if the flags indicate a digital mode
//
// Different sizes may be possible (e.g. for "72" modes), but the left offset does not change.
//
// The top-left origin of the Filtered bitmap should match the origin as displayed in the playback
// window (i.e. net of clipping).
//
// If the host needs the "raw" bitmap (e.g. to allow the user to edit the clipping outline), but the
// guest for some reason did not provide it (although it is always desirable), then the host
// will reconstruct a synthetic unclipped bitmap by adding blank borders (background color, if known,
// or black). If needed, the guest can use a similar approach rather than sending no "raw" bitmap at all.
//

// Return codes for RP_IPC_TO_GUEST_SCREENCAPTURE
// Note: the flags describe the "raw" image with 1:1 pixels, not the "filtered" one which is scaled

#define RP_SCREENCAPTURE_ERROR                      0x00000000  // non-error is always >= 1 (because of "MODE" flags)

#define RP_GUESTSCREENFLAGS_MODE_DIGITAL            0x00000001  // "RTG" on Amiga, might be referred to as VGA or other digital (non-TV) mode on other systems
#define RP_GUESTSCREENFLAGS_MODE_NTSC               0x00000002
#define RP_GUESTSCREENFLAGS_MODE_PAL                0x00000003
#define RP_GUESTSCREENFLAGS_MODEMASK                0x00000003
#define RP_GUESTSCREENFLAGS_MODE(m)                 ((m) & RP_GUESTSCREENFLAGS_MODEMASK) // returns RP_GUESTSCREENFLAGS_MODE_DIGITAL, RP_GUESTSCREENFLAGS_MODE_NTSC or RP_GUESTSCREENFLAGS_MODE_PAL

#define RP_GUESTSCREENFLAGS_VERTICAL_NONINTERLACED  0x00000000  // default also for RP_GUESTSCREENFLAGS_MODE_DIGITAL modes
#define RP_GUESTSCREENFLAGS_VERTICAL_INTERLACED     0x00000004
#define RP_GUESTSCREENFLAGS_VERTICALMASK            0x00000004
#define RP_GUESTSCREENFLAGS_VERTICAL(m)             ((m) & RP_GUESTSCREENFLAGS_VERTICALMASK) // returns RP_GUESTSCREENFLAGS_VERTICAL_NONINTERLACED o RP_GUESTSCREENFLAGS_VERTICAL_INTERLACED

#define RP_GUESTSCREENFLAGS_HORIZONTAL_LORES        0x00000008
#define RP_GUESTSCREENFLAGS_HORIZONTAL_HIRES        0x00000000  // default also for RP_GUESTSCREENFLAGS_MODE_DIGITAL modes
#define RP_GUESTSCREENFLAGS_HORIZONTAL_SUPERHIRES   0x00000018
#define RP_GUESTSCREENFLAGS_HORIZONTALMASK          0x00000018
#define RP_GUESTSCREENFLAGS_HORIZONTAL(m)           ((m) & RP_GUESTSCREENFLAGS_HORIZONTALMASK) // returns RP_GUESTSCREENFLAGS_HORIZONTAL_LORES, RP_GUESTSCREENFLAGS_HORIZONTAL_HIRES or RP_GUESTSCREENFLAGS_HORIZONTAL_SUPERHIRES


// RP_IPC_TO_HOST_HOSTVERSION
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +-----------------------+-------------------+-------------------+
//  |         major         |      minor        |      build        |
//  +-----------------------+-------------------+-------------------+
#define RP_HOSTVERSION_MAJOR(ver)    (((ver) >> 20) & 0xFFF)
#define RP_HOSTVERSION_MINOR(ver)    (((ver) >> 10) & 0x3FF)
#define RP_HOSTVERSION_BUILD(ver)    ((ver) & 0x3FF)
#define RP_MAKE_HOSTVERSION(major,minor,build) ((LPARAM) (((LPARAM)((major) & 0xFFF)<<20) | ((LPARAM)((minor) & 0x3FF)<<10) | ((LPARAM)((build) & 0x3FF))))


// RP_IPC_TO_HOST_PRIVATE_SHAREDEVENT wParam
#define RP_SHARED_EVENT_ESCAPE				1
#define	RP_SHARED_EVENT_TURBO				2
#define	RP_SHARED_EVENT_PAUSE				3
#define	RP_SHARED_EVENT_SCREENCAPTURECLIP	4
#define	RP_SHARED_EVENT_SCREENCAPTUREFILE	5
#define	RP_SHARED_EVENT_PLUGINOPTIONS		6
// RP_IPC_TO_HOST_PRIVATE_SHAREDEVENT lParam
#define	RP_SHARED_EVENT_PRESSED			0x0000
#define	RP_SHARED_EVENT_RELEASED		0x0001

// RP_IPC_TO_GUEST_PRIVATE_TYPECLIP return code
#define PRIVATETYPECLIP_NOTIMPL		0
#define PRIVATETYPECLIP_FAILED		1
#define PRIVATETYPECLIP_SUCCEDED	2
#define PRIVATETYPECLIP_INPROGRESS	3 // a RP_IPC_TO_HOST_PRIVATE_TYPECLIPDONE will be sent when done


// RPScreenOverlay (used by RP_IPC_TO_GUEST_SCREENOVERLAY)

typedef struct RPScreenOverlay
{
	DWORD dwIndex;	 // overlay index
	LONG  lLeft;	 // horizontal offset from screen left edge
	LONG  lTop;	     // vertical offset from screen top edge
	LONG  lWidth;	 // image width (if set to 0, clears a previously set overlay)
	LONG  lHeight;	 // image height (if set to 0, clears a previously set overlay)
	DWORD dwFormat;  // pixel format (see RPSOPF_* defines below)
	BYTE  btData[1]; // image data
} RPSCREENOVERLAY;

// RPSCREENOVERLAY dwFormat
#define RPSOPF_32BIT_BGRA  0 // 4 bytes per pixel (blue, green, red, alpha)


// RP_IPC_TO_HOST_PRIVATE_MENUEVENT wParam
#define RP_MENU_EVENT_RIGHT		1
#define RP_MENU_EVENT_LEFT		2
#define RP_MENU_EVENT_DOWN		3
#define RP_MENU_EVENT_UP		4
#define RP_MENU_EVENT_SELECT	5	
#define RP_MENU_EVENT_BACK		6


// Legacy Compatibility
#ifndef RP_NO_LEGACY
// Changed in 7.0
#define RP_MOUSECAPTURE_MAGICMOUSE RP_MOUSECAPTURE_INTEGRATED
// Changed in 3.0
#define RP_IPC_TO_HOST_DEVICECONTENT_LEGACY   (WM_APP + 16)
#define RP_IPC_TO_GUEST_DEVICECONTENT_LEGACY   (WM_APP + 205)
#define RPLATFORM_API_VER RETROPLATFORM_API_VER
#define RPLATFORM_API_VER_MAJOR RETROPLATFORM_API_VER_MAJOR
#define RPLATFORM_API_VER_MINOR RETROPLATFORM_API_VER_MINOR
#define RP_SCREENMODE_FULLWINDOW RP_SCREENMODE_FULLSCREEN_SHARED
#define RP_SCREENMODE_FULLSCREEN_1 RP_SCREENMODE_DISPLAY_FULLSCREEN_1
#define RP_SCREENMODE_FULLSCREEN_2 RP_SCREENMODE_DISPLAY_FULLSCREEN_2
#define RP_SCREENMODE_1X RP_SCREENMODE_SCALE_1X
#define RP_SCREENMODE_2X RP_SCREENMODE_SCALE_2X
#define RP_SCREENMODE_3X RP_SCREENMODE_SCALE_3X
#define RP_SCREENMODE_4X RP_SCREENMODE_SCALE_4X
#define RP_SCREENMODE_WW RP_SCREENMODE_SCALE_TARGET
#define RP_SCREENMODE_XX RP_SCREENMODE_SCALE_MAX
#define RP_SCREENMODE_MODEMASK RP_SCREENMODE_SCALEMASK
#define RP_SCREENMODE_MODE(m) RP_SCREENMODE_SCALE(m)
#define RP_SCREENMODE_SUBPIXEL RP_SCREENMODE_SCALING_SUBPIXEL
#define RP_SCREENMODE_STRETCH RP_SCREENMODE_SCALING_STRETCH
#define RP_FEATURE_RESIZE_SUBPIXEL RP_FEATURE_SCALING_SUBPIXEL
#define RP_FEATURE_RESIZE_STRETCH  RP_FEATURE_SCALING_STRETCH
typedef struct RPDeviceContent_Legacy
{
	BYTE  btDeviceCategory; // RP_DEVICE_* value
	BYTE  btDeviceNumber;   // device number (range 0..31)
	WCHAR szContent[1];     // full path and name of the image file to load, or input port device (Unicode, variable-sized field)
} RPDEVICECONTENTLEGACY;
#define RP_DEVICE_FLOPPY RP_DEVICECATEGORY_FLOPPY
#define RP_DEVICE_HD RP_DEVICECATEGORY_HD
#define RP_DEVICE_CD RP_DEVICECATEGORY_CD
#define RP_DEVICE_NET RP_DEVICECATEGORY_NET
#define RP_DEVICE_TAPE RP_DEVICECATEGORY_TAPE
#define RP_DEVICE_CARTRIDGE RP_DEVICECATEGORY_CARTRIDGE
#define RP_DEVICE_INPUTPORT RP_DEVICECATEGORY_INPUTPORT
#define RP_DEVICE_CATEGORIES RP_DEVICECATEGORY_COUNT
// Legacy Host Side Input Port Devices
#define RP_IPD_MOUSE1    L"Mouse1" // \0\0-terminated first mouse type ("Mouse1\0\0" = default Windows mouse, or exact mouse described as "Mouse1\\\?\HID#VID_046D&PID_C521&MI_00#8&3b7afb0d&0&0000#{378de44c-56ef-11d1-bc8c-00a0c91405dd\0\0")
#define RP_IPD_JOYSTICK1 L"Joystick1" // \0\0-terminated first joystick type (e.g. standard joystick for WinUAE, described as "Joystick1\0ProductGUID InstanceGUID\0ProductName\0\0"); ProductName must be stripped of trailing spaces, if any
#define RP_IPD_JOYSTICK2 L"Joystick2" // \0\0-terminated second joystick type (e.g. X-Arcade (Left) joystick for WinUAE, described as "Joystick2\0ProductGUID InstanceGUID\0ProductName\0\0"); ProductName must be stripped of trailing spaces, if any
#define RP_IPD_JOYSTICK3 L"Joystick3" // \0\0-terminated third joystick type (e.g. X-Arcade (Right) joystick for WinUAE, described as "Joystick3\0ProductGUID InstanceGUID\0ProductName\0\0"); ProductName must be stripped of trailing spaces, if any
#define RP_IPD_KEYBDL1   L"KeyboardLayout1" // \0\0-terminated first joystick emulation keyboard layout (e.g. Keyboard Layout A for WinUAE)
#define RP_IPD_KEYBDL2   L"KeyboardLayout2" // \0\0-terminated second joystick emulation keyboard layout (e.g. Keyboard Layout B for WinUAE)
#define RP_IPD_KEYBDL3   L"KeyboardLayout3" // \0\0-terminated third joystick emulation keyboard layout (e.g. Keyboard Layout C for WinUAE)
// Messages
#define RPIPCGM_REGISTER RP_IPC_TO_HOST_PRIVATE_REGISTER
#define RPIPCGM_FEATURES RP_IPC_TO_HOST_FEATURES
#define RPIPCGM_CLOSED RP_IPC_TO_HOST_CLOSED
#define RPIPCGM_ACTIVATED RP_IPC_TO_HOST_ACTIVATED
#define RPIPCGM_DEACTIVATED RP_IPC_TO_HOST_DEACTIVATED
#define RPIPCGM_ENABLED RP_IPC_TO_HOST_ENABLED
#define RPIPCGM_DISABLED RP_IPC_TO_HOST_DISABLED
#define RPIPCGM_SCREENMODE RP_IPC_TO_HOST_SCREENMODE
#define RPIPCGM_POWERLED RP_IPC_TO_HOST_POWERLED
#define RPIPCGM_DEVICES RP_IPC_TO_HOST_DEVICES
#define RPIPCGM_DEVICEACTIVITY RP_IPC_TO_HOST_DEVICEACTIVITY
#define RPIPCGM_MOUSECAPTURE RP_IPC_TO_HOST_MOUSECAPTURE
#define RPIPCGM_HOSTAPIVERSION RP_IPC_TO_HOST_HOSTAPIVERSION
#define RPIPCGM_PAUSE RP_IPC_TO_HOST_PAUSE
#define RPIPCGM_TURBO RP_IPC_TO_HOST_TURBO
#define RPIPCGM_PING RP_IPC_TO_HOST_PING
#define RPIPCGM_VOLUME RP_IPC_TO_HOST_VOLUME
#define RPIPCGM_PARENT RP_IPC_TO_HOST_PARENT
#define RPIPCGM_DEVICESEEK RP_IPC_TO_HOST_DEVICESEEK
#define RPIPCGM_CLOSE RP_IPC_TO_HOST_CLOSE
#define RPIPCGM_DEVICEREADWRITE RP_IPC_TO_HOST_DEVICEREADWRITE
#define RPIPCGM_HOSTVERSION RP_IPC_TO_HOST_HOSTVERSION
#define RPIPCGM_INPUTDEVICE RP_IPC_TO_HOST_INPUTDEVICE
#define RPIPCGM_DEVICECONTENT RP_IPC_TO_HOST_DEVICECONTENT
#define RPIPCHM_CLOSE RP_IPC_TO_GUEST_CLOSE
#define RPIPCHM_SCREENMODE RP_IPC_TO_GUEST_SCREENMODE
#define RPIPCHM_SCREENCAPTURE RP_IPC_TO_GUEST_SCREENCAPTURE
#define RPIPCHM_PAUSE RP_IPC_TO_GUEST_PAUSE
#define RPIPCHM_RESET RP_IPC_TO_GUEST_RESET
#define RPIPCHM_TURBO RP_IPC_TO_GUEST_TURBO
#define RPIPCHM_PING RP_IPC_TO_GUEST_PING
#define RPIPCHM_VOLUME RP_IPC_TO_GUEST_VOLUME
#define RPIPCHM_EVENT RP_IPC_TO_GUEST_EVENT
#define RPIPCHM_MOUSECAPTURE RP_IPC_TO_GUEST_MOUSECAPTURE
#define RPIPCHM_SAVESTATE RP_IPC_TO_GUEST_SAVESTATE
#define RPIPCHM_LOADSTATE RP_IPC_TO_GUEST_LOADSTATE
#define RPIPCHM_FLUSH RP_IPC_TO_GUEST_FLUSH
#define RPIPCHM_DEVICEREADWRITE RP_IPC_TO_GUEST_DEVICEREADWRITE
#define RPIPCHM_QUERYSCREENMODE RP_IPC_TO_GUEST_QUERYSCREENMODE
#define RPIPCHM_GUESTAPIVERSION RP_IPC_TO_GUEST_GUESTAPIVERSION
#define RPIPCHM_DEVICECONTENT RP_IPC_TO_GUEST_DEVICECONTENT
#define RP_FEATURE_TURBO RP_FEATURE_TURBO_CPU
#endif
// End of Legacy Compatibility

#endif // __CLOANTO_RETROPLATFORMIPC_H__
