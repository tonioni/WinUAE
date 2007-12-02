/*****************************************************************************
 Name    : RetroPlatformIPC.h
 Project : RetroPlatform Player
 Client  : Cloanto Italia srl
 Legal   : Copyright © 2007 Cloanto Italia srl - All rights reserved. This
         : file is made available under the terms of the GNU General Public
         : License version 2 as published by the Free Software Foundation.
 Authors : os, mcb
 Created : 2007-08-27 13:55:49
 Comment : RP Player interprocess communication include file
 *****************************************************************************/

#ifndef __CLOANTO_RETROPLATFORMIPC_H__
#define __CLOANTO_RETROPLATFORMIPC_H__

#include <windows.h>

#define RPLATFORM_API_VER       "1.0"
#define RPLATFORM_API_VER_MAJOR  1
#define RPLATFORM_API_VER_MINOR  0

#define RPIPC_HostWndClass   "RetroPlatformHost%s"
#define RPIPC_GuestWndClass  "RetroPlatformGuest%d"


// ****************************************************************************
//  Guest-to-Host Messages
// ****************************************************************************

// Message:
//    RPIPCGM_REGISTER
// Description:
//    this is a private message and is automatically sent
//    by the RPInitializeGuest() function
//    to register the caller as a RetroPlatform guest
//
#define RPIPCGM_REGISTER	(WM_APP + 0)

// Message:
//    RPIPCGM_FEATURES
// Description:
//    the guest uses this message to tell the host
//    about the features it supports;
//    at startup time, the guest sends initialization messages
//    so that the host can adapt the GUI based on the reported features;
//    these messages include:
//       RPIPCGM_FEATURES (describes guest features),
//       RPIPCGM_POWERLED (turns on the power LED in the GUI),
//       RPIPCGM_DEVICES (one for each device category: tells the number of emulated devices),
//       RPIPCGM_DEVICEIMAGE (one for each device with an image file loaded),
//       RPIPCGM_TURBO (tells if some of the turbo modes are activated from the start),
//       RPIPCGM_INPUTMODE (tells if keyboard mode or joystick mode is active),
//       RPIPCGM_VOLUME (reports about starting volume level),
//       RPIPCGM_SCREENMODE (communicates the screen mode and the guest window handle);
//    note that at startup time the guest should create a borderless and hidden window
//    and send its handle using a RPIPCGM_SCREENMODE message, which must be the last
//    of the initialization messages, since it displays the guest window
//    and the host "frame window" (the part of the player user interface
//    with command and status icons which can be used to drag the guest window, etc.)
// Data sent:
//    WPARAM = RP_FEATURE_* flags
// Response:
//    none
//
#define RPIPCGM_FEATURES	(WM_APP + 1)

// Message:
//    RPIPCGM_CLOSED
// Description:
//    this message is sent to the host when the guest is terminating
// Data sent:
//    none
// Response:
//    none
//
#define RPIPCGM_CLOSED	(WM_APP + 2)

// Message:
//    RPIPCGM_ACTIVATED
// Description:
//    the guest sends this message to the host
//    when its window is being activated
// Data sent:
//    LPARAM = identifier of the thread that owns the window being deactivated
// Response:
//    none
//
#define RPIPCGM_ACTIVATED	(WM_APP + 3)

// Message:
//    RPIPCGM_DEACTIVATED
// Description:
//    the guest sends this message to the host
//    when its window is being deactivated
// Data sent:
//    LPARAM = identifier of the thread that owns the window being activated
// Response:
//    none
//
#define RPIPCGM_DEACTIVATED	(WM_APP + 4)

// Message:
//    RPIPCGM_ZORDER
// Description:
//    the guest sends this message to notify the host about a change
//    in the Z order (front-to-back position) of its window
//    (e.g. the user clicked the window icon in the application bar
//    to bring the window to the front)
// Data sent:
//    none
// Response:
//    none
//
#define RPIPCGM_ZORDER	(WM_APP + 5)

// Message:
//    RPIPCGM_MINIMIZED
// Description:
//    the guest sends this message to the host when its window
//    has been minimized
//    (e.g. using the Minimize menu command in the application bar)
// Data sent:
//    none
// Response:
//    none
//
#define RPIPCGM_MINIMIZED	(WM_APP + 6)

// Message:
//    RPIPCGM_RESTORED
// Description:
//    the guest sends this message to the host when its window
//    has been restored from the minimized status
//    (e.g. using the Restore menu command in the application bar)
// Data sent:
//    none
// Response:
//    none
//
#define RPIPCGM_RESTORED	(WM_APP + 7)

// Message:
//    RPIPCGM_MOVED
// Description:
//    the guest sends this message to the host when its window position
//    has been changed
//    (e.g. using the Move menu command in the application bar)
// Data sent:
//    none
// Response:
//    none
//
#define RPIPCGM_MOVED	(WM_APP + 8)

// Message:
//    RPIPCGM_SCREENMODE
// Description:
//    the guest sends a RPIPCGM_SCREENMODE message to notify the host
//    about a change in its "screen mode" (1x/2x/4x/full screen, etc.);
//    screen mode changes requested by the host
//    (see the RPIPCHM_SCREENMODE message) must not be notified,
//    unless this is an asynchronous screen mode change
//    (i.e. the guest returned the INVALID_HANDLE_VALUE
//    response to a RPIPCHM_SCREENMODE host request);
//    this message can also be sent when the guest has to close
//    and reopen its window for other reasons;
//    at startup-time, the guest must create
//    a borderless and hidden window and send its handle
//    using this message; the host will then take care
//    of preparing, positioning and showing the guest window
// Data sent:
//    WPARAM = new screen mode (RP_SCREENMODE_* value)
//    LPARAM = handle of the (new) guest window
// Response:
//    none
//
#define RPIPCGM_SCREENMODE	(WM_APP + 9)

// Message:
//    RPIPCGM_POWERLED
// Description:
//    sent to the host to change the power LED state
// Data sent:
//    WPARAM = power LED intensity (min/off 0, max 100)
// Response:
//    none
//
#define RPIPCGM_POWERLED	(WM_APP + 10)

// Message:
//    RPIPCGM_DEVICES
// Description:
//    this message is used to notify the host about a change
//    in the number of emulated devices (floppy drives, hard disks, etc.)
// Data sent:
//    WPARAM = device category (RP_DEVICE_* value)
//    LPARAM = 32-bit bitfield representing the devices
//             emulated in the specified category
//             (every bit set to 1 corresponds to a mounted drive
//              e.g. 0x00000005 = drive 0 and drive 2 are emulated)
// Response:
//    none
//
#define RPIPCGM_DEVICES	(WM_APP + 11)

// Message:
//    RPIPCGM_DEVICEACTIVITY
// Description:
//    this message can be used to turn on or off the activity indicator
//    of a specified device (like a LED on the original hardware);
//    the indicator can also be "blinked", i.e. the host will turn the
//    LED on and then off again after the specified amount of time
// Data sent:
//    WPARAM = device category (RP_DEVICE_* value) and device number
//             combined with the MAKEWORD macro;
//             e.g. MAKEWORD(RP_DEVICE_FLOPPY, 0)
//    LPARAM = 0 turns off the activity LED,
//             (LPARAM)-1 turns on the activity LED,
//             <millisecond delay> turns on the activity LED
//             for the specified amount of time (blink)
// Response:
//    none
//
#define RPIPCGM_DEVICEACTIVITY	(WM_APP + 12)

// Message:
//    RPIPCGM_MOUSECAPTURE
// Description:
//    the guest sends this message when the mouse is captured/released
//    (the mouse is "captured" when its movements are restricted to the guest window area
//    and the system cursor is not visible);
//    for consistency across different guests, a guest which sends RPIPCGM_MOUSECAPTURE
//    messages should also implement a keyboard-actuated mouse release functionality
//    (the preferred key for this purpose is included in the parameters sent from the
//    host at startup time - see RPLaunchGuest() in RetroPlatformPlugin.h);
//    note that in order not to interfere with the window dragging functionality,
//    the mouse should not be captured when the guest window gets the focus,
//    but when a mouse button event is received
// Data sent:
//    WPARAM = non-zero if the mouse has been captured,
//             zero if the mouse has been released
// Response:
//    none
//
#define RPIPCGM_MOUSECAPTURE	(WM_APP + 13)

// Message:
//    RPIPCGM_HOSTAPIVERSION
// Description:
//     the guest can send a RPIPCGM_HOSTAPIVERSION to query the host
//     about the RetroPlatform API version it implements;
//     since the guest plugin already asks for a minimim version of the API
//     on the host side, this message can be used to check the host API version
//     and enable optional functionality
// Data sent:
//    none
// Response:
//    LRESULT = major and minor version combined with the MAKELONG macro
//              (e.g. LOWORD(lr) = major version; HIWORD(lr) = minor version)
//
#define RPIPCGM_HOSTAPIVERSION	(WM_APP + 14)

// Message:
//    RPIPCGM_PAUSE
// Description:
//    the guest sends this message to the host
//    when it enters or exits pause mode;
//    pause mode changes requested by the host
//    (see the RPIPCHM_PAUSE message) must not be notified;
//    note: when paused, the guest should release the mouse (if captured)
// Data sent:
//    WPARAM = non-zero when the guest enters pause mode
//             or zero when the guest exits from pause mode
// Response:
//    none
//
#define RPIPCGM_PAUSE	(WM_APP + 15)

// Message:
//    RPIPCGM_DEVICEIMAGE
// Description:
//    the guest sends a RPIPCGM_DEVICEIMAGE message
//    to notify the host that an image file has been loaded into
//    (or ejected from) an emulated device;
//    this notification must not be sent when the event
//    has been requested by the host (see the RPIPCHM_DEVICEIMAGE message)
// Data sent:
//    pData = a RPDEVICEIMAGE structure (see below);
//            the szImageFile field of the structure
//            contains an empty string when the guest
//            is ejecting an image file from the device
// Response:
//    none
//
#define RPIPCGM_DEVICEIMAGE	(WM_APP + 16)

// Message:
//    RPIPCGM_TURBO
// Description:
//    the guest sends a RPIPCGM_TURBO message
//    to notify the host about activation of "turbo" (maximum speed) mode
//    of some of its functionalities (e.g. floppy, CPU);
//    turbo mode activations/deactivations requested by the host
//    (see the RPIPCHM_TURBO message) must not be notified;
// Data sent:
//    WPARAM = mask of functionalities affected (RP_TURBO_* flags)
//    LPARAM = bits corresponding to those set in WPARAM
//             (1 = turbo mode activated for the guest functionality
//              0 = guest functionality reverted to normal speed)
// Response:
//    none
//
#define RPIPCGM_TURBO	(WM_APP + 17)

// Message:
//    RPIPCGM_INPUTMODE
// Description:
//    the RPIPCGM_INPUTMODE message can be used
//    to notify the host about activation
//    of the specified input mode (e.g. keyboard vs. game controller)
//    of the guest; input mode changes requested by the host
//    (see the RPIPCHM_INPUTMODE message) must not be notified
// Data sent:
//    WPARAM = input mode activated (RP_INPUTMODE_* value)
// Response:
//    none
//
#define RPIPCGM_INPUTMODE	(WM_APP + 18)

// Message:
//    RPIPCGM_VOLUME
// Description:
//    the guest uses the RPIPCGM_VOLUME message
//    to notify the host about a change of its audio level;
//    audio level changes requested by the host
//    (see the RPIPCHM_VOLUME message) must not be notified
// Data sent:
//    WPARAM = volume level (min/off 0, max 100)
// Response:
//    none
//
#define RPIPCGM_VOLUME	(WM_APP + 19)




// ****************************************************************************
//  Host-to-Guest Messages
// ****************************************************************************

// Message:
//    RPIPCHM_CLOSE
// Description:
//    sent from the host when the emulation must be terminated
//    (e.g. the user has hit the close button in the host window);
//    the guest should destroy its window and terminate (see Response below)
// Data sent:
//    none
// Response:
//    LRESULT = non-zero if the guest can safely terminate or 0 otherwise
//
#define RPIPCHM_CLOSE	(WM_APP + 200)

// Message:
//    RPIPCHM_MINIMIZE
// Description:
//    sent from the host when the emulation window must be minimized
// Data sent:
//    none
// Response:
//    LRESULT = non-zero if the guest can minimized its window or 0 otherwise
//
#define RPIPCHM_MINIMIZE	(WM_APP + 201)

// Message:
//    RPIPCHM_SCREENMODE
// Description:
//    this message is sent to ask the guest to activate a specified screen mode;
//    when switching to the new screen mode, the guest can resize (reuse) its window
//    or close its window and open a new one
// Data sent:
//    WPARAM = RP_SCREENMODE_* value
// Response:
//    LRESULT = handle of the (new) guest window
//              or NULL (the screen mode couldn't be changed)
//              or INVALID_HANDLE_VALUE (the screen mode will be changed asynchronously
//              and the host will soon get a RPIPCGM_SCREENMODE notification)
//
#define RPIPCHM_SCREENMODE	(WM_APP + 202)

// Message:
//    RPIPCHM_SCREENCAPTURE
// Description:
//    with this message the host asks the guest to save its screen
//    to the the specified file in BMP format
// Data sent:
//    pData = (Unicode) full path and name of the file to save
//            (note: the file may exist and can be overwritten)
// Response:
//    LRESULT = non-zero if the guest saved its screen to the file
//
#define RPIPCHM_SCREENCAPTURE	(WM_APP + 203)

// Message:
//    RPIPCHM_PAUSE
// Description:
//    the RPIPCHM_PAUSE message sets the guest into pause mode
//    or resumes the guest from pause mode;
//    note: when paused, the guest should release the mouse (if captured)
// Data sent:
//    WPARAM = non-zero to set the guest into pause mode
//             or zero to resume the guest from pause mode
// Response:
//    LRESULT = non-zero if the guest executed the command
//
#define RPIPCHM_PAUSE	(WM_APP + 204)

// Message:
//    RPIPCHM_DEVICEIMAGE
// Description:
//    the host sends a RPIPCHM_DEVICEIMAGE message
//    to load an image file into an emulated device
//    (e.g. an ADF floppy file into a floppy drive)
//    or to unload the currently loaded image from the device
// Data sent:
//    pData = a RPDEVICEIMAGE structure (see below);
//            if the szImageFile field of the structure
//            contains an empty string, the guest should
//            unload the current image file from the device
// Response:
//    LRESULT = non-zero if the guest executed the command
//
#define RPIPCHM_DEVICEIMAGE	(WM_APP + 205)

// Message:
//    RPIPCHM_RESET
// Description:
//    the host sends this message to reset the guest
// Data sent:
//    WPARAM = a RP_RESET_* value
// Response:
//    LRESULT = non-zero if the guest executed the command
//
#define RPIPCHM_RESET	(WM_APP + 206)

// Message:
//    RPIPCHM_TURBO
// Description:
//    the host sends this message to activate or deactivate
//    the turbo mode of selected guest functionalities
// Data sent:
//    WPARAM = mask of functionalities to change (RP_TURBO_* flags)
//    LPARAM = bits corresponding to those set in WPARAM
//             (1 = speedup the guest functionality
//              0 = revert to normal speed emulation)
// Response:
//    LRESULT = non-zero if the guest executed the command
//
#define RPIPCHM_TURBO	(WM_APP + 207)

// Message:
//    RPIPCHM_INPUTMODE
// Description:
//    the RPIPCHM_INPUTMODE message activates
//    the specified input mode of the guest
// Data sent:
//    WPARAM = input mode (RP_INPUTMODE_* value)
// Response:
//    LRESULT = non-zero if the guest executed the command
//
#define RPIPCHM_INPUTMODE	(WM_APP + 208)

// Message:
//    RPIPCHM_VOLUME
// Description:
//    the host uses the RPIPCHM_VOLUME message to set
//    the audio level of the guest
// Data sent:
//    WPARAM = volume level (min 0, max 100)
// Response:
//    LRESULT = non-zero if the guest set the volume as requested
//
#define RPIPCHM_VOLUME	(WM_APP + 209)

// Message:
//    RPIPCHM_RELEASEMOUSEKEY
// Description:
//    the host uses the RPIPCHM_RELEASEMOUSEKEY message
//    to change the release mouse key information
// Data sent:
//    WPARAM = VK_* identifier of the mouse-release key
//             (e.g. "0x1B" for the Esc key - see VK_* constants in winuser.h)
//    LPARAM = milliseconds value
//             (amount of time the user has to hold the above key to release the mouse)
// Response:
//    LRESULT = non-zero if the guest accepted the new settings
//
#define RPIPCHM_RELEASEMOUSEKEY	(WM_APP + 210)

// Message:
//    RPIPCHM_EVENT
// Description:
//    the host uses the RPIPCHM_EVENT message
//    to simulate keyboard, mouse, joystick (press/release)
//    and other guest-specific events
// Data sent:
//    pData = (Unicode) event string (guest-specific)
// Response:
//    LRESULT = non-zero if the guest simulated the specified event
//
#define RPIPCHM_EVENT	(WM_APP + 211)



// ****************************************************************************
//  Message Data Structures and Defines
// ****************************************************************************

// Guest Features
#define RP_FEATURE_POWERLED      0x00000001 // a power LED is emulated
#define RP_FEATURE_SCREEN1X      0x00000002 // 1x windowed mode is available
#define RP_FEATURE_SCREEN2X      0x00000004 // 2x windowed mode is available
#define RP_FEATURE_SCREEN4X      0x00000008 // 4x windowed mode is available
#define RP_FEATURE_FULLSCREEN    0x00000010 // full screen display is available
#define RP_FEATURE_SCREENCAPTURE 0x00000020 // screen capture functionality is available (see RPIPCHM_SCREENCAPTURE message)
#define RP_FEATURE_PAUSE         0x00000040 // pause functionality is available (see RPIPCHM_PAUSE message)
#define RP_FEATURE_TURBO         0x00000080 // turbo mode functionality is available (see RPIPCHM_TURBO message)
#define RP_FEATURE_INPUTMODE     0x00000100 // input mode switching is supported (see RPIPCHM_INPUTMODE message)
#define RP_FEATURE_VOLUME        0x00000200 // volume adjustment is possible (see RPIPCHM_VOLUME message)

// Screen Modes
#define RP_SCREENMODE_1X          0 // 1x windowed mode
#define RP_SCREENMODE_2X          1 // 2x windowed mode
#define RP_SCREENMODE_4X          2 // 4x windowed mode
#define RP_SCREENMODE_FULLSCREEN  3 // full screen

// Device Categories
#define RP_DEVICE_FLOPPY    0 // floppy disk drive
#define RP_DEVICE_HD        1 // hard disk drive
#define RP_DEVICE_CD        2 // CD/DVD drive
#define RP_DEVICE_NET       3 // network card
#define RP_DEVICE_TAPE      4 // cassette tape drive
#define RP_DEVICE_CARTRIDGE 5 // expansion cartridge

typedef struct RPDeviceImage
{
	BYTE  btDeviceCategory;  // RP_DEVICE_* value
	BYTE  btDeviceNumber;    // device number (range 0..31)
	WCHAR szImageFile[1];    // full path and name of the image file to load (Unicode, variable-sized field)
} RPDEVICEIMAGE;

// Turbo Mode Functionalities
#define RP_TURBO_CPU     0x00000001 // CPU
#define RP_TURBO_FLOPPY  0x00000002 // floppy disk drive
#define RP_TURBO_TAPE    0x00000004 // cassette tape drive

// Input Modes
#define RP_INPUTMODE_KEYBOARD  0 // the keyboard is used to simulate a joystick
#define RP_INPUTMODE_JOYSTICK  1 // use the joystick connected to the system

// Reset Type
#define RP_RESET_SOFT  0 // soft reset
#define RP_RESET_HARD  1 // hard reset


#endif // __CLOANTO_RETROPLATFORMIPC_H__
