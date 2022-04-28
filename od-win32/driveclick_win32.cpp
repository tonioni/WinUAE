/*
* UAE - The Un*x Amiga Emulator
*
* PC drive Drive Click Emulation Support
*
* Copyright 2006 Toni Wilen
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef DRIVESOUND

#include "uae.h"
#include "options.h"
#include "driveclick.h"
#include "threaddep/thread.h"

#include <windows.h>
#include "fdrawcmd.h"

int driveclick_pcdrivemask;

#define DC_PIPE_SIZE 100
static smp_comm_pipe dc_pipe[DC_PIPE_SIZE];
static HANDLE h[2] = { INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE };
static int motors[2];

static int CmdMotor (HANDLE h_, BYTE motor_)
{
	DWORD dwRet;
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	return !!DeviceIoControl(h_, motor_ ? IOCTL_FD_MOTOR_ON : IOCTL_FD_MOTOR_OFF,
		NULL, 0, NULL, 0, &dwRet, NULL);
}

static int CmdSeek (HANDLE h_, BYTE cyl_)
{
	DWORD dwRet;
	FD_SEEK_PARAMS sp = { cyl_, 0 };
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	return !!DeviceIoControl(h_, IOCTL_FDCMD_SEEK, &sp, sizeof(sp), NULL, 0, &dwRet, NULL);
}

static int CmdSpecify (HANDLE h_, BYTE srt_, BYTE hut_, BYTE hlt_, BYTE nd_)
{
	DWORD dwRet;
	FD_SPECIFY_PARAMS sp = { (BYTE)((srt_ << 4) | (hut_ & 0x0f)), (BYTE)((hlt_ << 1) | (nd_ & 1)) };
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	return !!DeviceIoControl(h_, IOCTL_FDCMD_SPECIFY, &sp, sizeof(sp), NULL, 0, &dwRet, NULL);
}

static int SetDataRate (HANDLE h_, BYTE bDataRate_)
{
	DWORD dwRet;
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	return !!DeviceIoControl(h_, IOCTL_FD_SET_DATA_RATE, &bDataRate_, sizeof(bDataRate_), NULL, 0, &dwRet, NULL);
}

static int SetMotorDelay (HANDLE h_, BYTE delay_)
{
	DWORD dwRet;
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	return !!DeviceIoControl(h_, IOCTL_FD_SET_MOTOR_TIMEOUT, &delay_, sizeof(delay_), NULL, 0, &dwRet, NULL);
}

void driveclick_fdrawcmd_seek(int drive, int cyl)
{
	write_comm_pipe_int (dc_pipe, (drive << 8) | cyl, 1);
}
void driveclick_fdrawcmd_motor (int drive, int running)
{
	write_comm_pipe_int (dc_pipe, 0x8000 | (drive << 8) | (running ? 1 : 0), 1);
}

void driveclick_fdrawcmd_vsync(void)
{
	int i;
	for (i = 0; i < 2; i++) {
		if (motors[i] > 0) {
			motors[i]--;
			if (motors[i] == 0)
				CmdMotor(h[i], 0);
		}
	}
}

static void driveclick_thread (void *v)
{
	for (;;) {
		int drive, cyl;
		int v = read_comm_pipe_int_blocking (dc_pipe);
		if (v < 0)
			break;
		drive = (v >> 8) & 3;
		if (v & 0x8000) {
			int motor = v & 1;
			motors[drive] = motor ? -1 : 0;
			CmdMotor(h[drive], motor);
		} else {
			cyl = v & 255;
			if (motors[drive] == 0)
				motors[drive] = 100;
			CmdSeek(h[drive], cyl);
		}
	}
}

static int driveclick_fdrawcmd_init(int drive)
{
	static int thread_ok;

	if (h[drive] == INVALID_HANDLE_VALUE)
		return 0;
	motors[drive] = 0;
	SetDataRate(h[drive], 3);
	CmdSpecify(h[drive], 0xd, 0xf, 0x1, 0);
	SetMotorDelay(h[drive], 0);
	CmdMotor(h[drive], 0);
	if (thread_ok)
		return 1;
	thread_ok = 1;
	init_comm_pipe (dc_pipe, DC_PIPE_SIZE, 3);
	uae_start_thread (_T("fdrawcmd_win32"), driveclick_thread, NULL, NULL);
	return 1;
}

void driveclick_fdrawcmd_close(int drive)
{
	if (h[drive] != INVALID_HANDLE_VALUE)
		CloseHandle(h[drive]);
	h[drive] = INVALID_HANDLE_VALUE;
	motors[drive] = 0;
}

static int driveclick_fdrawcmd_open_2(int drive)
{
	TCHAR s[32];

	driveclick_fdrawcmd_close(drive);
	_stprintf (s, _T("\\\\.\\fdraw%d"), drive);
	h[drive] = CreateFile(s, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h[drive] == INVALID_HANDLE_VALUE)
		return 0;
	return 1;
}

int driveclick_fdrawcmd_open(int drive)
{
	if (!driveclick_fdrawcmd_open_2(drive))
		return 0;
	driveclick_fdrawcmd_init(drive);
	return 1;
}

void driveclick_fdrawcmd_detect(void)
{
	static int detected;
	if (detected)
		return;
	detected = 1;
	if (driveclick_fdrawcmd_open_2(0))
		driveclick_pcdrivemask |= 1;
	driveclick_fdrawcmd_close(0);
	if (driveclick_fdrawcmd_open_2(1))
		driveclick_pcdrivemask |= 2;
	driveclick_fdrawcmd_close(1);
}

#endif
