/*****************************************************************************
 Name    : RetroPlatformIPC.h
 Project : RetroPlatform Player
 Client  : Cloanto Italia srl
 Support : http://www.retroplatform.com
 Legal   : Copyright 2007-2011 Cloanto Italia srl - All rights reserved. This
         : file is made available under the terms of the GNU General Public
         : License version 2 as published by the Free Software Foundation.
 Authors : os, mcb
 Created : 2007-08-27 13:55:49
 Updated : 2011-02-02 12:20:00
 Comment : RP Player interprocess communication include file
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORMIPC_H__
#define __CLOANTO_RETROPLATFORMIPC_H__

#include <windows.h>

#define RPLATFORM_API_VER       "1.4"
#define RPLATFORM_API_VER_MAJOR  1
#define RPLATFORM_API_VER_MINOR  4

#define RPIPC_HostWndClass   "RetroPlatformHost%s"
#define RPIPC_GuestWndClass  "RetroPlatformGuest%d"


// ****************************************************************************
//  Guest-to-Host Messages
// ****************************************************************************

#define RPIPCGM_REGISTER        (WM_APP + 0)
#define RPIPCGM_FEATURES        (WM_APP + 1)
#define RPIPCGM_CLOSED          (WM_APP + 2)
#define RPIPCGM_ACTIVATED       (WM_APP + 3)
#define RPIPCGM_DEACTIVATED     (WM_APP + 4)
#define RPIPCGM_ENABLED         (WM_APP + 5)
#define RPIPCGM_DISABLED        (WM_APP + 6)
#define RPIPCGM_SCREENMODE      (WM_APP + 9)
#define RPIPCGM_POWERLED        (WM_APP + 10)
#define RPIPCGM_DEVICES         (WM_APP + 11)
#define RPIPCGM_DEVICEACTIVITY  (WM_APP + 12)
#define RPIPCGM_MOUSECAPTURE    (WM_APP + 13)
#define RPIPCGM_HOSTAPIVERSION  (WM_APP + 14)
#define RPIPCGM_PAUSE           (WM_APP + 15)
#define RPIPCGM_DEVICECONTENT   (WM_APP + 16)
#define RPIPCGM_TURBO           (WM_APP + 17)
#define RPIPCGM_PING            (WM_APP + 18)
#define RPIPCGM_VOLUME          (WM_APP + 19)
#define RPIPCGM_ESCAPED         (WM_APP + 20)
#define RPIPCGM_PARENT          (WM_APP + 21)
#define RPIPCGM_DEVICESEEK      (WM_APP + 22)
#define RPIPCGM_CLOSE           (WM_APP + 23)
#define RPIPCGM_DEVICEREADWRITE (WM_APP + 24)
#define RPIPCGM_HOSTVERSION     (WM_APP + 25)

// ****************************************************************************
//  Host-to-Guest Messages
// ****************************************************************************

#define RPIPCHM_CLOSE           (WM_APP + 200)
#define RPIPCHM_SCREENMODE      (WM_APP + 202)
#define RPIPCHM_SCREENCAPTURE   (WM_APP + 203)
#define RPIPCHM_PAUSE           (WM_APP + 204)
#define RPIPCHM_DEVICECONTENT   (WM_APP + 205)
#define RPIPCHM_RESET           (WM_APP + 206)
#define RPIPCHM_TURBO           (WM_APP + 207)
#define RPIPCHM_PING            (WM_APP + 208)
#define RPIPCHM_VOLUME          (WM_APP + 209)
#define RPIPCHM_ESCAPEKEY       (WM_APP + 210)
#define RPIPCHM_EVENT           (WM_APP + 211)
#define RPIPCHM_MOUSECAPTURE    (WM_APP + 212)
#define RPIPCHM_SAVESTATE       (WM_APP + 213)
#define RPIPCHM_LOADSTATE       (WM_APP + 214)
#define RPIPCHM_FLUSH           (WM_APP + 215)
#define RPIPCHM_DEVICEREADWRITE (WM_APP + 216)
#define RPIPCHM_QUERYSCREENMODE (WM_APP + 217)

// ****************************************************************************
//  Message Data Structures and Defines
// ****************************************************************************

// Guest Features
#define RP_FEATURE_POWERLED        0x00000001 // a power LED is emulated
#define RP_FEATURE_SCREEN1X        0x00000002 // 1x mode is available
#define RP_FEATURE_SCREEN2X        0x00000004 // 2x mode is available
#define RP_FEATURE_SCREEN3X        0x00000008 // 3x mode is available
#define RP_FEATURE_SCREEN4X        0x00000010 // 4x mode is available
#define RP_FEATURE_FULLSCREEN      0x00000020 // full screen display is available
#define RP_FEATURE_SCREENCAPTURE   0x00000040 // screen capture functionality is available (see RPIPCHM_SCREENCAPTURE message)
#define RP_FEATURE_PAUSE           0x00000080 // pause functionality is available (see RPIPCHM_PAUSE message)
#define RP_FEATURE_TURBO           0x00000100 // turbo mode functionality is available (see RPIPCHM_TURBO message)
#define RP_FEATURE_VOLUME          0x00000200 // volume adjustment is possible (see RPIPCHM_VOLUME message)
#define RP_FEATURE_STATE           0x00000400 // loading and saving of emulation state is supported (see RPIPCHM_SAVESTATE/RPIPCHM_LOADSTATE message)
#define RP_FEATURE_SCANLINES       0x00000800 // scan lines video effect is available
#define RP_FEATURE_DEVICEREADWRITE 0x00001000 // device read/write can be set at runtime on floppy and hard disks

// Screen Modes
#define RP_SCREENMODE_1X            0x00000000 // 1x window or full-screen mode ("CGA mode")
#define RP_SCREENMODE_2X            0x00000001 // 2x window or full-screen mode ("VGA mode")
#define RP_SCREENMODE_3X            0x00000002 // 3x window or full-screen mode ("triple CGA mode")
#define RP_SCREENMODE_4X            0x00000003 // 4x window or full-screen mode ("double VGA mode")
#define RP_SCREENMODE_XX            0x000000FF // autoset maximum nX (integer n, preserve ratio)
#define RP_SCREENMODE_MODEMASK      0x000000FF
#define RP_SCREENMODE_FULLSCREEN_1	0x00000100 // full screen on primary (default) display
#define RP_SCREENMODE_FULLSCREEN_2	0x00000200 // full screen on secondary display (fallback to 1 if unavailable)
#define RP_SCREENMODE_DISPLAYMASK	0x0000FF00
#define RP_SCREENMODE_FULLWINDOW	0x00010000 // use "full window" when in fullscreen (no gfx card full screen)
#define RP_SCREENMODE_USETVM_NEVER  0x00000000 // never use TV modes
#define RP_SCREENMODE_USETVM_ALWAYS 0x00020000 // always use TV modes
#define RP_SCREENMODE_USETVM_AUTO   0x00040000 // use all available modes
#define RP_SCREENMODE_USETVMMASK    0x00060000
#define RP_SCREENMODE_SCANLINES     0x00080000 // show video scan lines
#define RP_SCREENMODE_DISPLAY(m)    (((m) & RP_SCREENMODE_DISPLAYMASK) >> 8) // given a mode 'm' returns the display number (1-255) or 0 if full screen is not active
#define RP_SCREENMODE_USETVM(m)     ((m) & RP_SCREENMODE_USETVMMASK) // given a mode 'm' returns the RP_SCREENMODE_USETVM_* value in it (automatic display mode selection in full screen modes)
#define RP_SCREENMODE_MODE(m)       ((m) & RP_SCREENMODE_MODEMASK) // given a mode 'm' returns the #X mode

// Clip Flags (used only from host to guest, never from guest to host)
#define RP_CLIPFLAGS_AUTOCLIP		0x00000001 // ignore all 4 Clip values (same as all values = -1) and use "smart" offset and size
#define RP_CLIPFLAGS_NOCLIP			0x00000002 // ignore all 4 Clip values (same as all values = -1) and use 0:0 offset and maximum possible size (probably ugly, but good for adjusting clip area manually)

// Clip/Scale Examples
//
// An Amiga game with known clip offset/size will have lClipLeft/Top/Width/Height set, and no RP_CLIPFLAGS.
// In windowed mode, the guest (e.g. WinUAE) will take the net clipped region and apply RP_SCREENMODE_xX scaling to that.
// In RP_SCREENMODE_FULLWINDOW mode, the net clipped region will be "soft-scaled" to the maximum possible (not necessarily an integer scaling factor), centered and padded by a black border.
// In one of the RP_SCREENMODE_FULLSCREEN modes, the net clipped region will be centered in the smallest possible compatible hardware mode (in consideration of RP_SCREENMODE_USETVM) and padded by a black border.
// If an Amiga application sets a different Amiga chipset screen mode, the "container" window size will remain unchanged.
// If an Amiga application sets a different RTG screen mode, the "container" window size will reflect the new RTG size (instead of the Amiga clip size) and apply RP_SCREENMODE_xX.
//
// An unknown Amiga application or one that has no known clip offset/size will start with RP_CLIPFLAGS_AUTOCLIP.
// The guest (e.g. WinUAE) will apply whatever logic it can to minimize the visible overscan region.
// The guest will send to the host the actual RPScreenMode data with the offset/size details that were applied.
// In windowed mode, RP_SCREENMODE_xX scaling is applied like in the previous example.
// RP_SCREENMODE_FULLWINDOW and RP_SCREENMODE_FULLSCREEN modes behave like in the previous example (scaling, etc.)
// If an Amiga application sets a different Amiga or RTG chipset screen mode, the "container" window size may change.
//
// If the user wants to adjust clipping, or for automated grabs and calculations, it is possible to set RP_CLIPFLAGS_NOCLIP, which will widen the window to the maximum.
//
// Whenever the guest sets or changes the "container" window size (initially, or due to a command it receives, or due to Amiga-sourced changes), it sends a RPScreenMode update to the host.

typedef struct RPScreenMode
{
	DWORD dwScreenMode; // RP_SCREENMODE_* values and flags
	LONG lClipLeft;     // -1 = ignore (0 is a valid value)
	LONG lClipTop;      // -1 = ignore (0 is a valid value)
	LONG lClipWidth;    // -1 = ignore
	LONG lClipHeight;   // -1 = ignore
	HWND hGuestWindow;  // only valid for RPIPCGM_SCREENMODE
	DWORD dwClipFlags;	
} RPSCREENMODE;

// Device Categories
#define RP_DEVICE_FLOPPY     0 // floppy disk drive
#define RP_DEVICE_HD         1 // hard disk drive
#define RP_DEVICE_CD         2 // CD/DVD drive
#define RP_DEVICE_NET        3 // network card
#define RP_DEVICE_TAPE       4 // cassette tape drive
#define RP_DEVICE_CARTRIDGE  5 // expansion cartridge
#define RP_DEVICE_INPUTPORT  6 // input port
#define RP_DEVICE_CATEGORIES 7 // total number of device categories

#define RP_ALL_DEVICES      32 // constant for the RPIPCGM_DEVICEACTIVITY message

typedef struct RPDeviceContent
{
	BYTE  btDeviceCategory; // RP_DEVICE_* value
	BYTE  btDeviceNumber;   // device number (range 0..31)
	WCHAR szContent[1];     // full path and name of the image file to load, or input port device (Unicode, variable-sized field)
} RPDEVICECONTENT;

// Input Port Devices
#define RP_IPD_MOUSE1    L"Mouse1" // first mouse type (e.g. Windows Mouse for WinUAE)
#define RP_IPD_MOUSE2    L"Mouse2" // second mouse type (e.g. Mouse for WinUAE)
#define RP_IPD_MOUSE3    L"Mouse3" // third mouse type (e.g. Mousehack Mouse for WinUAE)
#define RP_IPD_MOUSE4    L"Mouse4" // fourth mouse type (e.g. RAW Mouse for WinUAE)
#define RP_IPD_JOYSTICK1 L"Joystick1" // first joystick type (e.g. standard joystick for WinUAE, described as "Joystick1\ProductGUID InstanceGUID\ProductName")
#define RP_IPD_JOYSTICK2 L"Joystick2" // second joystick type (e.g. X-Arcade (Left) joystick for WinUAE, described as "Joystick2\ProductGUID InstanceGUID\ProductName")
#define RP_IPD_JOYSTICK3 L"Joystick3" // third joystick type (e.g. X-Arcade (Right) joystick for WinUAE, described as "Joystick3\ProductGUID InstanceGUID\ProductName")
#define RP_IPD_KEYBDL1   L"KeyboardLayout1" // first joystick emulation keyboard layout (e.g. Keyboard Layout A for WinUAE)
#define RP_IPD_KEYBDL2   L"KeyboardLayout2" // second joystick emulation keyboard layout (e.g. Keyboard Layout B for WinUAE)
#define RP_IPD_KEYBDL3   L"KeyboardLayout3" // third joystick emulation keyboard layout (e.g. Keyboard Layout C for WinUAE)

// Joystick status flags
#define RP_JOYSTICK_RIGHT    0x00000001 // right direction
#define RP_JOYSTICK_LEFT     0x00000002 // left direction
#define RP_JOYSTICK_DOWN     0x00000004 // down direction
#define RP_JOYSTICK_UP       0x00000008 // up direction
#define RP_JOYSTICK_BUTTON1  0x00000010 // button 1 - Fire 1 - CD32 Red
#define RP_JOYSTICK_BUTTON2  0x00000020 // button 2 - Fire 2 - CD32 Blue
#define RP_JOYSTICK_BUTTON3  0x00000040 // button 3 - Fire 3 - CD32 Yellow
#define RP_JOYSTICK_BUTTON4  0x00000080 // button 4 - Fire 4 - CD32 Green
#define RP_JOYSTICK_BUTTON5  0x00000100 // button 5 - CD32 Play
#define RP_JOYSTICK_BUTTON6  0x00000200 // button 6 - CD32 Reverse 
#define RP_JOYSTICK_BUTTON7  0x00000400 // button 7 - CD32 Forward

// Device Read/Write status
#define RP_DEVICE_READONLY   0 // the medium is write-protected
#define RP_DEVICE_READWRITE  1 // the medium is read/write

// Turbo Mode Functionalities
#define RP_TURBO_CPU     0x00000001 // CPU
#define RP_TURBO_FLOPPY  0x00000002 // floppy disk drive
#define RP_TURBO_TAPE    0x00000004 // cassette tape drive

// Reset Type
#define RP_RESET_SOFT  0 // soft reset
#define RP_RESET_HARD  1 // hard reset

// RPIPCGM_MOUSECAPTURE/RPIPCHM_MOUSECAPTURE
#define RP_MOUSECAPTURE_CAPTURED     0x00000001
#define RP_MOUSECAPTURE_MAGICMOUSE   0x00000002

// RPIPCGM_DEVICEACTIVITY
#define RP_DEVICEACTIVITY_GREEN    0x0000 // green led
#define RP_DEVICEACTIVITY_RED      0x0001 // red led
#define RP_DEVICEACTIVITY_READ     RP_DEVICEACTIVITY_GREEN // device activity is a read operation
#define RP_DEVICEACTIVITY_WRITE    RP_DEVICEACTIVITY_RED   // device activity is a write operation

// RPIPCGM_HOSTVERSION
//   3 3 2 2 2 2 2 2 2 2 2 2 1 1 1 1 1 1 1 1 1 1
//   1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0 9 8 7 6 5 4 3 2 1 0
//  +-----------------------+-------------------+-------------------+
//  |         major         |      minor        |      build        |
//  +-----------------------+-------------------+-------------------+
#define RP_HOSTVERSION_MAJOR(ver)    (((ver) >> 20) & 0xFFF)
#define RP_HOSTVERSION_MINOR(ver)    (((ver) >> 10) & 0x3FF)
#define RP_HOSTVERSION_BUILD(ver)    ((ver) & 0x3FF)
#define RP_MAKE_HOSTVERSION(major,minor,build) ((LPARAM) (((LPARAM)((major) & 0xFFF)<<20) | ((LPARAM)((minor) & 0x3FF)<<10) | ((LPARAM)((build) & 0x3FF))))


#endif // __CLOANTO_RETROPLATFORMIPC_H__
