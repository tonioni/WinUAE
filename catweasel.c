
#include <stdio.h>

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef CATWEASEL

#include "options.h"
#include "memory.h"
#include "ioport.h"
#include "catweasel.h"
#include "uae.h"
#include "zfile.h"

#include <catweasl_usr.h>

struct catweasel_contr cwc;

static int cwhsync, cwmk3buttonsync;
static int cwmk3port, cwmk3port1, cwmk3port2;
static int handshake;
static int mouse_x[2], mouse_y[2], mouse_px[2], mouse_py[2];

static HANDLE handle = INVALID_HANDLE_VALUE;

int catweasel_isjoystick(void)
{
    uae_u8 b = cwc.can_joy;
    if (b) {
	if (cwc.type == CATWEASEL_TYPE_MK3 && cwc.sid[0])
	    b |= 0x80;
	if (cwc.type >= CATWEASEL_TYPE_MK4)
	    b |= 0x80;
    }
    return b;
}
int catweasel_ismouse(void)
{
    return cwc.can_mouse;
}

static void sid_write(uae_u8 reg, uae_u8 val, int sidnum)
{
    if (sidnum >= cwc.can_sid)
	return;
    catweasel_do_bput(0xd8, val);
    catweasel_do_bput(0xdc, reg | (sidnum << 7));
    catweasel_do_bget(0xd8); // dummy read
    catweasel_do_bget(0xd8); // dummy read
}

static uae_u8 sid_read(uae_u8 reg, int sidnum)
{
    if (sidnum >= cwc.can_sid)
	return 0;
    catweasel_do_bput(0xdc, 0x20 | reg | (sidnum << 7));
    catweasel_do_bget(0xd8); // dummy read
    catweasel_do_bget(0xd8); // dummy read
    return catweasel_do_bget(0xd8);
}

static uae_u8 get_buttons(void)
{
    uae_u8 b, b2;

    b = 0;
    if (cwc.type < CATWEASEL_TYPE_MK3)
	return b;
    b2 = catweasel_do_bget(0xc8) & (0x80 | 0x40);
    if (!(b2 & 0x80))
	b |= 0x80;
    if (!(b2 & 0x40))
	b |= 0x08;
    if (cwc.type >= CATWEASEL_TYPE_MK4) {
	b &= ~0x80;
	catweasel_do_bput(3, 0x81);
	if (!(catweasel_do_bget(0x07) & 0x10))
	    b |= 0x80;
	b2 = catweasel_do_bget(0xd0) ^ 15;
	catweasel_do_bput(3, 0x41);
	if (cwc.sid[0]) {
	    b2 &= ~(1 | 2);
	    if (sid_read(0x19, 0) > 0x7f)
		b2 |= 2;
	    if (sid_read(0x1a, 0) > 0x7f)
		b2 |= 1;
	}
	if (cwc.sid[1]) {
	    b2 &= ~(4 | 8);
	    if (sid_read(0x19, 1) > 0x7f)
		b2 |= 8;
	    if (sid_read(0x1a, 1) > 0x7f)
		b2 |= 4;
	}
    } else {
	b2 = cwmk3port1 | (cwmk3port2 << 2);
    }
    b |= (b2 & (8 | 4)) << 3;
    b |= (b2 & (1 | 2)) << 1;
    return b;
}

int catweasel_read_mouse(int port, int *dx, int *dy, int *buttons)
{
    if (!cwc.can_mouse)
	return 0;
    *dx = mouse_x[port];
    mouse_x[port] = 0;
    *dy = mouse_y[port];
    mouse_y[port] = 0;
    *buttons = (get_buttons() >> (port * 4)) & 15;
    return 1;
}

static void sid_reset(void)
{
    int i;
    for (i = 0; i < 0x19; i++) {
	sid_write(i, 0, 0);
	sid_write(i, 0, 1);
    }
}

static void catweasel_detect_sid(void)
{
    int i, j;
    uae_u8 b1, b2;

    cwc.sid[0] = cwc.sid[1] = 0;
    if (!cwc.can_sid)
	return;
    sid_reset();
    if (cwc.type >= CATWEASEL_TYPE_MK4) {
	catweasel_do_bput(3, 0x81);
	b1 = catweasel_do_bget(0xd0);
	for (i = 0; i < 100; i++) {
	    sid_read(0x19, 0); // delay
	    b2 = catweasel_do_bget(0xd0);
	    if ((b1 & 3) != (b2 & 3))
		cwc.sid[0] = 6581;
	    if ((b1 & 12) != (b2 & 12))
		cwc.sid[1] = 6581;
	}
    }
    catweasel_do_bput(3, 0x41);
    for (i = 0; i < 2 ;i++) {
	sid_reset();
	sid_write(0x0f, 0xff, i);
	sid_write(0x12, 0x10, i);
	for(j = 0; j != 1000; j++) {
	    sid_write(0, 0, i);
	    if((sid_read(0x1b, i) & 0x80) != 0) {
		cwc.sid[i] = 6581;
		break;
	    }
	}
	sid_reset();
	sid_write(0x0f, 0xff, i);
	sid_write(0x12, 0x30, i);
	for(j = 0; j != 1000; j++) {
	    sid_write(0, 0, i);
	    if((sid_read(0x1b, i) & 0x80) != 0) {
		cwc.sid[i] = 8580;
		break;
	    }
	}
    }
    sid_reset();
}

void catweasel_hsync (void)
{
    int i;

    if (cwc.type < CATWEASEL_TYPE_MK3)
	return;
    cwhsync--;
    if (cwhsync > 0)
	return;
    cwhsync = 10;
    if (handshake) {
	/* keyboard handshake */
	catweasel_do_bput(0xd0, 0);
	handshake = 0;
    }
    if (cwc.type == CATWEASEL_TYPE_MK3 && cwc.sid[0]) {
	uae_u8 b;
	cwmk3buttonsync--;
	if (cwmk3buttonsync <= 0) {
	    cwmk3buttonsync = 30;
	    b = 0;
	    if (sid_read(0x19, 0) > 0x7f)
		b |= 2;
	    if (sid_read(0x1a, 0) > 0x7f)
		b |= 1;
	    if (cwmk3port == 0) {
		cwmk3port1 = b;
		catweasel_do_bput(0xd4, 0); // select port2
		cwmk3port = 1;
	    } else {
		cwmk3port2 = b;
		catweasel_do_bget(0xd4); // select port1
		cwmk3port = 0;
	    }
	}
    }
    if (!cwc.can_mouse)
	return;
    /* read MK4 mouse counters */
    catweasel_do_bput(3, 0x81);
    for (i = 0; i < 2; i++) {
	int x, y, dx, dy;
	x = (uae_s8)catweasel_do_bget(0xc4 + i * 8);
	y = (uae_s8)catweasel_do_bget(0xc0 + i * 8);
	dx = mouse_px[i] - x;
	if (dx > 127)
	    dx = 255 - dx;
	if (dx < -128)
	    dx = 255 + dx;
	dy = mouse_py[i] - y;
	if (dy > 127)
	    dy = 255 - dy;
	if (dy < -128)
	    dy = 255 + dy;
	mouse_x[i] -= dx;
	mouse_y[i] -= dy;
	mouse_px[i] = x;
	mouse_py[i] = y;
    }
    catweasel_do_bput(3, 0x41);
}

int catweasel_read_joystick (uae_u8 *dir, uae_u8 *buttons)
{
    if (!cwc.can_joy)
	return 0;
    *dir = catweasel_do_bget(0xc0);
    *buttons = get_buttons();
    return 1;
}

int catweasel_read_keyboard (uae_u8 *keycode)
{
    uae_u8 v;

    if (!cwc.can_kb)
	return 0;
    v = catweasel_do_bget (0xd4);
    if (!(v & 0x80))
	return 0;
    if (handshake)
	return 0;
    *keycode = catweasel_do_bget (0xd0);
    catweasel_do_bput (0xd0, 0);
    handshake = 1;
    return 1;
}

uae_u32	catweasel_do_bget (uaecptr addr)
{
    DWORD did_read = 0;
    uae_u8 buf1[1], buf2[1];

    if (addr >= 0x100)
	return 0;
    buf1[0] = (uae_u8)addr;
    if (handle != INVALID_HANDLE_VALUE)
	DeviceIoControl (handle, CW_PEEKREG_FULL, buf1, 1, buf2, 1, &did_read, 0);
    else
	buf2[0] = ioport_read (cwc.iobase + addr);
    //write_log ("G %02.2X %02.2X %d\n", buf1[0], buf2[0], did_read);
    return buf2[0];
}

void catweasel_do_bput (uaecptr	addr, uae_u32 b)
{
    uae_u8 buf[2];
    DWORD did_read = 0;

    if (addr >= 0x100)
	return;
    buf[0] = (uae_u8)addr;
    buf[1] = b;
    if (handle != INVALID_HANDLE_VALUE)
	DeviceIoControl (handle, CW_POKEREG_FULL, buf, 2, 0, 0, &did_read, 0);
    else
	ioport_write (cwc.iobase + addr, b);
    //write_log ("P %02.2X %02.2X %d\n", (uae_u8)addr, (uae_u8)b, did_read);
}

#include "core.cw4.c"

static int cw_config_done(void)
{
    return ioport_read (cwc.iobase + 7) & 4;
}
static int cw_fpga_ready(void)
{
    return ioport_read (cwc.iobase + 7) & 8;
}
static void cw_resetFPGA(void)
{
    ioport_write (cwc.iobase + 2, 227);
    ioport_write (cwc.iobase + 3, 0);
    sleep_millis (10);
    ioport_write (cwc.iobase + 3, 65);
}

static int catweasel3_configure(void)
{
    ioport_write (cwc.iobase, 241);
    ioport_write (cwc.iobase + 1, 0);
    ioport_write (cwc.iobase + 2, 0);
    ioport_write (cwc.iobase + 4, 0);
    ioport_write (cwc.iobase + 5, 0);
    ioport_write (cwc.iobase + 0x29, 0);
    ioport_write (cwc.iobase + 0x2b, 0);
    return 1;
}

static int catweasel4_configure(void)
{
    struct zfile *f;
    time_t t;

    ioport_write (cwc.iobase, 241);
    ioport_write (cwc.iobase + 1, 0);
    ioport_write (cwc.iobase + 2, 227);
    ioport_write (cwc.iobase + 3, 65);
    ioport_write (cwc.iobase + 4, 0);
    ioport_write (cwc.iobase + 5, 0);
    ioport_write (cwc.iobase + 0x29, 0);
    ioport_write (cwc.iobase + 0x2b, 0);
    sleep_millis(10);

    if (cw_config_done()) {
	write_log ("CW: FPGA already configured, skipping core upload\n");
	return 1;
    }
    cw_resetFPGA();
    sleep_millis(10);
    if (cw_config_done()) {
	write_log ("CW: FPGA failed to reset!\n");
	return 0;
    }
    f = zfile_fopen("core.cw4", "rb");
    if (!f) {
	f = zfile_fopen_data ("core.cw4.gz", core_len, core);
	f = zfile_gunzip (f);
    }
    write_log ("CW: starting core upload, this will take few seconds\n");
    t = time(NULL) + 10; // give up if upload takes more than 10s
    for (;;) {
	uae_u8 b;
	if (zfile_fread (&b, 1, 1, f) != 1)
	    break;
	ioport_write (cwc.iobase + 3, (b & 1) ? 67 : 65);
	while (!cw_fpga_ready()) {
	    if (time(NULL) >= t) {
		write_log ("CW: FPGA core upload got stuck!?\n");
		cw_resetFPGA();
		return 0;
	    }
	}
	ioport_write (cwc.iobase + 192, b);
    }
    if (!cw_config_done()) {
	write_log ("CW: FPGA didn't accept the core!\n");
	cw_resetFPGA();
	return 0;
    }
    sleep_millis(10);
    write_log ("CW: core uploaded successfully\n");
    return 1;
}

#include <setupapi.h>
#include <cfgmgr32.h>

#define PCI_CW_MK3 "PCI\\VEN_E159&DEV_0001&SUBSYS_00021212"
#define PCI_CW_MK4 "PCI\\VEN_E159&DEV_0001&SUBSYS_00035213"
#define PCI_CW_MK4_BUG "PCI\\VEN_E159&DEV_0001&SUBSYS_00025213"

extern int os_winnt;
int force_direct_catweasel;
static int direct_detect(void)
{
    HDEVINFO devs;
    SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;
    SP_DEVINFO_DATA devInfo;
    int devIndex;
    int cw = 0;

    if (!os_winnt)
	return 0;
    devs = SetupDiGetClassDevsEx(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT, NULL, NULL, NULL);
    if (devs == INVALID_HANDLE_VALUE)
	return 0;
    devInfoListDetail.cbSize = sizeof(devInfoListDetail);
    if(SetupDiGetDeviceInfoListDetail(devs,&devInfoListDetail)) {
        devInfo.cbSize = sizeof(devInfo);
	for(devIndex=0;SetupDiEnumDeviceInfo(devs,devIndex,&devInfo);devIndex++) {
	    TCHAR devID[MAX_DEVICE_ID_LEN];
	    if(CM_Get_Device_ID_Ex(devInfo.DevInst,devID,MAX_DEVICE_ID_LEN,0,devInfoListDetail.RemoteMachineHandle)!=CR_SUCCESS)
		devID[0] = TEXT('\0');
	    if (!memcmp (devID, PCI_CW_MK3, strlen (PCI_CW_MK3))) {
		if (cw > 3)
		    break;
		cw = 3;
	    }
	    if (!memcmp (devID, PCI_CW_MK4, strlen (PCI_CW_MK4)) ||
		!memcmp (devID, PCI_CW_MK4_BUG, strlen (PCI_CW_MK4_BUG)))
		cw = 4;
	    if (cw) {
		SP_DEVINFO_LIST_DETAIL_DATA devInfoListDetail;
		ULONG status = 0;
		ULONG problem = 0;
		LOG_CONF config = 0;
		BOOL haveConfig = FALSE;
		ULONG dataSize;
		PBYTE resDesData;
	        RES_DES prevResDes, resDes;
	        RESOURCEID resId = ResType_IO;

		devInfoListDetail.cbSize = sizeof(devInfoListDetail);
		if((!SetupDiGetDeviceInfoListDetail(devs,&devInfoListDetail)) ||
			(CM_Get_DevNode_Status_Ex(&status,&problem,devInfo.DevInst,0,devInfoListDetail.RemoteMachineHandle)!=CR_SUCCESS))
		    break;
		if(!(status & DN_HAS_PROBLEM)) {
		    if (CM_Get_First_Log_Conf_Ex(&config,
						 devInfo.DevInst,
						 ALLOC_LOG_CONF,
						 devInfoListDetail.RemoteMachineHandle) == CR_SUCCESS) {
			haveConfig = TRUE;
		    }
		}
		if(!haveConfig) {
		    if (CM_Get_First_Log_Conf_Ex(&config,
						 devInfo.DevInst,
						 FORCED_LOG_CONF,
						 devInfoListDetail.RemoteMachineHandle) == CR_SUCCESS) {
			haveConfig = TRUE;
		    }
		}
		if(!haveConfig) {
		    if(!(status & DN_HAS_PROBLEM) || (problem != CM_PROB_HARDWARE_DISABLED)) {
			if (CM_Get_First_Log_Conf_Ex(&config,
						     devInfo.DevInst,
						     BOOT_LOG_CONF,
						     devInfoListDetail.RemoteMachineHandle) == CR_SUCCESS) {
			    haveConfig = TRUE;
			}
		    }
		}
		if(!haveConfig)
		    break;
		prevResDes = (RES_DES)config;
		resDes = 0;
		while(CM_Get_Next_Res_Des_Ex(&resDes,prevResDes,ResType_IO,&resId,0,NULL)==CR_SUCCESS) {
		    if(prevResDes != config)
			CM_Free_Res_Des_Handle(prevResDes);
		    prevResDes = resDes;
		    if(CM_Get_Res_Des_Data_Size_Ex(&dataSize,resDes,0,NULL)!=CR_SUCCESS)
			continue;
		    resDesData = malloc (dataSize);
		    if(!resDesData)
			continue;
		    if(CM_Get_Res_Des_Data_Ex(resDes,resDesData,dataSize,0,NULL)!=CR_SUCCESS) {
			free (resDesData);
			continue;
		    }
		    if (resId == ResType_IO) {
			PIO_RESOURCE pIoData = (PIO_RESOURCE)resDesData;
			if(pIoData->IO_Header.IOD_Alloc_End-pIoData->IO_Header.IOD_Alloc_Base+1) {
			    write_log("CW: PCI SCAN: CWMK%d @%I64X - %I64X\n", cw,
				pIoData->IO_Header.IOD_Alloc_Base,pIoData->IO_Header.IOD_Alloc_End);
			    cwc.iobase = (int)pIoData->IO_Header.IOD_Alloc_Base;
			    cwc.direct_type = cw;
			}
		    }
		    free (resDesData);
		}
	        if(prevResDes != config)
		    CM_Free_Res_Des_Handle(prevResDes);
	        CM_Free_Log_Conf_Handle(config);
	    }
	}
    }
    SetupDiDestroyDeviceInfoList(devs);
    if (cw) {
	if (!ioport_init ())
	    cw = 0;
    }
    return cw;
}

int catweasel_init(void)
{
    char name[32], tmp[1000];
    int i, len;
    uae_u8 buffer[10000];
    uae_u32 model, base;
    int detect = 0;

    if (cwc.type)
	return 1;

    if (force_direct_catweasel >= 100) {

        cwc.iobase = force_direct_catweasel & 0xffff;
	if (force_direct_catweasel > 0xffff) {
	    cwc.direct_type = force_direct_catweasel >> 16;
	} else {
	    cwc.direct_type = force_direct_catweasel >= 0x400 ? 3 : 1;
	}

    } else {

	for (i = 0; i < 4; i++) {
	    if (currprefs.catweasel > 0)
		i = currprefs.catweasel;
	    sprintf (name, "\\\\.\\CAT%d_F0", i);
	    handle = CreateFile (name, GENERIC_READ|GENERIC_WRITE, FILE_SHARE_WRITE|FILE_SHARE_READ, 0,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	    if (handle != INVALID_HANDLE_VALUE || currprefs.catweasel > 0)
		break;
	}
	if (handle == INVALID_HANDLE_VALUE)
	    catweasel_detect();
    }

    if (handle == INVALID_HANDLE_VALUE) {
        strcpy(name, "[DIRECT]");
	if (cwc.direct_type && ioport_init()) {
	    if (cwc.direct_type == 4 && catweasel4_configure()) {
		cwc.type = 4;
		cwc.can_joy = 2;
		cwc.can_sid = 2;
		cwc.can_kb = 1;
		cwc.can_mouse = 2;
	    } else if (cwc.direct_type == 3 && catweasel3_configure()) {
		cwc.type = 3;
		cwc.can_joy = 1;
		cwc.can_sid = 1;
		cwc.can_kb = 1;
		cwc.can_mouse = 0;
	    }
	}
	if (cwc.type == 0) {
	    write_log ("CW: No Catweasel detected\n");
	    goto fail;
	}
    }

    if (!cwc.direct_type) {
	if (!DeviceIoControl (handle, CW_GET_VERSION, 0, 0, buffer, sizeof (buffer), &len, 0)) {
	    write_log ("CW: CW_GET_VERSION failed %d\n", GetLastError());
	    goto fail;
	}
	write_log ("CW driver version string '%s'\n", buffer);
	if (!DeviceIoControl (handle, CW_GET_HWVERSION, 0, 0, buffer, sizeof (buffer), &len, 0)) {
	    write_log ("CW: CW_GET_HWVERSION failed %d\n", GetLastError());
	    goto fail;
	}
	write_log ("CW: v=%d 14=%d 28=%d 56=%d joy=%d dpm=%d sid=%d kb=%d sidfifo=%d\n",
	    buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
	    buffer[6], buffer[7], ((uae_u32*)(buffer + 8))[0]);
	cwc.can_joy = (buffer[4] & 1) ? 2 : 0;
	cwc.can_sid = buffer[6] ? 1 : 0;
	cwc.can_kb = buffer[7] & 1;
	cwc.can_mouse = (buffer[4] & 2) ? 2 : 0;
	if (!DeviceIoControl (handle, CW_LOCK_EXCLUSIVE, 0, 0, buffer, sizeof (buffer), &len, 0)) {
	    write_log ("CW: CW_LOCK_EXCLUSIVE failed %d\n", GetLastError());
	    goto fail;
	}
	model = *((uae_u32*)(buffer + 4));
	base = *((uae_u32*)(buffer + 0));
	cwc.type = model == 0 ? 1 : model == 2 ? 4 : 3;
	cwc.iobase = base;
	if (cwc.type == CATWEASEL_TYPE_MK4 && cwc.can_sid)
	    cwc.can_sid = 2;
    }

    if (cwc.type == CATWEASEL_TYPE_MK4) {
	if (cwc.can_mouse) {
	    int i;
	    catweasel_do_bput(3, 0x81);
	    catweasel_do_bput(0xd0, 4|8); // amiga mouse + pullups
	    // clear mouse counters
	    for (i = 0; i < 2; i++) {
		catweasel_do_bput(0xc4 + i * 8, 0);
		catweasel_do_bput(0xc0 + i * 8, 0);
	    }
	}
	catweasel_do_bput(3, 0x41); /* enable MK3-mode */
    }
    if (cwc.can_joy)
	catweasel_do_bput(0xcc, 0); // joystick buttons = input

    catweasel_init_controller(&cwc);
    sprintf(tmp, "CW: Catweasel MK%d @%p (%s) enabled.",
	cwc.type, (uae_u8*)cwc.iobase, name);
    if (cwc.can_sid) {
	char *p = tmp + strlen(tmp);
	catweasel_detect_sid();
	sprintf(p, " SID0=%d", cwc.sid[0]);
	if (cwc.can_sid > 1) {
	    p += strlen(p);
	    sprintf(p, " SID1=%d", cwc.sid[1]);
	}
    }
    write_log("%s\n", tmp);

    return 1;
fail:
    catweasel_free ();
    return 0;

}

void catweasel_free (void)
{
    if (cwc.type == 4)
	catweasel_do_bput(3, 0x61); // enable floppy passthrough
    if (handle != INVALID_HANDLE_VALUE)
	CloseHandle (handle);
    handle = INVALID_HANDLE_VALUE;
    ioport_free();
    memset (&cwc, 0, sizeof cwc);
    mouse_x[0] = mouse_x[1] = mouse_y[0] = mouse_y[1] = 0;
    mouse_px[0] = mouse_px[1] = mouse_py[0] = mouse_py[1] = 0;
    cwmk3port = cwmk3port1 = cwmk3port2 = 0;
    cwhsync = cwmk3buttonsync = 0;
}

int catweasel_detect (void)
{
    char name[32];
    int i;
    static int detected;

    if (detected)
	return detected < 0 ? 0 : 1;

    detected = -1;
    for (i = 0; i < 4; i++) {
	sprintf (name, "\\\\.\\CAT%u_F0", i);
	handle = CreateFile (name, GENERIC_READ, FILE_SHARE_WRITE|FILE_SHARE_READ, 0,
	    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	if (handle != INVALID_HANDLE_VALUE) {
	    CloseHandle (handle);
	    write_log("CW: Windows driver device detected '%s'\n", name);
	    detected = 1;
	    return TRUE;
	}
    }
    if (handle == INVALID_HANDLE_VALUE) {
	if (force_direct_catweasel >= 100) {
	    if (ioport_init()) 
		return TRUE;
	    return FALSE;
	}
	if (direct_detect()) {
	    detected = 1;
	    return TRUE;
	}
    }
    return FALSE;
}

#define outb(v,port) catweasel_do_bput(port,v)
#define inb(port) catweasel_do_bget(port)

#define LONGEST_TRACK 16000

static uae_u8 mfmbuf[LONGEST_TRACK * 4];
static uae_u8 tmpmfmbuffer[LONGEST_TRACK * 2];

static int bitshiftcompare(uae_u8 *src,int bit,int len,uae_u8 *comp)
{
	uae_u8 b;
	int ones,zeros,len2;

	ones=zeros=0;
	len2=len;
	while(len--) {
		b = (comp[0] << bit) | (comp[1] >> (8 - bit));
		if(b != *src) return 1;
		if(b==0x00) zeros++;
		if(b==0xff) ones++;
		src++;
		comp++;
	}
	if(ones==len2||zeros==len2) return 1;
	return 0;
}

static uae_u8 *mergepieces(uae_u8 *start,int len,int bits,uae_u8 *sync)
{
	uae_u8 *dst=tmpmfmbuffer;
	uae_u8 b;
	int size;
	int shift;
	
	size=len-(sync-start);
	memcpy(dst,sync,size);
	dst+=size;
	b=start[len];
	b&=~(255>>bits);
	b|=start[0]>>bits;
	*dst++=b;
	shift=8-bits;
	while(start<=sync+2000) {
		*dst++=(start[0]<<shift)|(start[1]>>(8-shift));
		start++;
	}
	return tmpmfmbuffer;
}	

#define SCANOFFSET 1 /*	scanning range in bytes, -SCANOFFSET to SCANOFFSET */
#define SCANOFFSET2 20
#define SCANLENGHT 200 /* scanning length in bytes */

static uae_u8* scantrack(uae_u8 *sync1,uae_u8 *sync2,int *trackbytes,int *trackbits)
{
	int i,bits,bytes,matched;
	uae_u8 *sync2bak=sync2;
	
	sync1+=SCANOFFSET2;
	sync2+=SCANOFFSET2;
	while(sync1 < sync2bak - 2*SCANOFFSET - SCANOFFSET2 - SCANLENGHT) {
		matched=0x7fff;
		for(i=0;i<2*SCANOFFSET*8;i++) {
			bits=i&7;
			bytes=-SCANOFFSET+(i>>3);
			if(!bitshiftcompare(sync1,bits,SCANLENGHT,sync2+bytes)) {
				if(matched==0x7fff) {
					matched=i;
				} else {
					break;
				}
			}
		}
		if(matched!=0x7fff && i>=2*SCANOFFSET*8) {
			bits=matched&7;
			bytes=-SCANOFFSET+(matched>>3);
			*trackbytes=sync2+bytes-sync1;
			*trackbits=bits;
			return mergepieces(sync1,*trackbytes,*trackbits,sync2bak);
		}
		sync1++;
		sync2++;
	}
	return 0;
}	

static unsigned char threshtab[128];

static void codec_makethresh(int trycnt, const unsigned char *origt, unsigned char *t, int numthresh)
{
    static unsigned char tab[10] = { 0, 0, 0, 0, -1, -2, 1, 2, -1, 1 };

    if (trycnt >= sizeof (tab))
	trycnt = sizeof (tab) - 1;
    while(numthresh--)
	t[numthresh] = origt[numthresh] + tab[trycnt];
}

static void codec_init_threshtab(int trycnt, const unsigned char *origt)
{
    static unsigned char old_thresholds[2] = { 0, 0 };
    unsigned char t[2];
    int a, i;

    codec_makethresh(trycnt, origt, t, 2);

    if(*(unsigned short*)t == *(unsigned short*)old_thresholds)
	return;

    for(i=0,a=2; i<128; i++) {
	if(i == t[0] || i == t[1])
	    a++;
	threshtab[i] = a;
    }

    *(unsigned short*)&old_thresholds = *(unsigned short*)t;
}

static __inline__ void CWSetCReg(catweasel_contr *c, unsigned char clear, unsigned char set)
{
    c->control_register = (c->control_register & ~clear) | set;
    outb(c->control_register, c->io_sr);
}

static void CWTriggerStep(catweasel_contr *c)
{
    CWSetCReg(c, c->crm_step, 0);
    CWSetCReg(c, 0, c->crm_step);
}

void catweasel_init_controller(catweasel_contr *c)
{
    int i, j;

    if(!c->iobase)
	return;

    switch(c->type) {
    case CATWEASEL_TYPE_MK1:
	c->crm_sel0 = 1 << 5;
	c->crm_sel1 = 1 << 4;
	c->crm_mot0 = 1 << 3;
	c->crm_mot1 = 1 << 7;
	c->crm_dir  = 1 << 1;
	c->crm_step = 1 << 0;
	c->srm_trk0 = 1 << 4;
	c->srm_dchg = 1 << 5;
	c->srm_writ = 1 << 1;
	c->io_sr    = c->iobase + 2;
	c->io_mem   = c->iobase;
	break;
    case CATWEASEL_TYPE_MK3:
    case CATWEASEL_TYPE_MK4:
	c->crm_sel0 = 1 << 2;
	c->crm_sel1 = 1 << 3;
	c->crm_mot0 = 1 << 1;
	c->crm_mot1 = 1 << 5;
	c->crm_dir  = 1 << 4;
	c->crm_step = 1 << 7;
	c->srm_trk0 = 1 << 2;
	c->srm_dchg = 1 << 5;
	c->srm_writ = 1 << 6;
	c->srm_dskready = 1 << 4;
	c->io_sr    = c->iobase + 0xe8;
	c->io_mem   = c->iobase + 0xe0;
	break;
    default:
	return;
    }

    c->control_register = 255;

    /* select all drives, step inside */
    CWSetCReg(c, c->crm_dir | c->crm_sel0 | c->crm_sel1, 0);
    for(i=0;i<2;i++) {
	c->drives[i].number = i;
	c->drives[i].contr = c;
	c->drives[i].diskindrive = 0;
	
	/* select only the respective drive, step to track 0 */
	if(i == 0) {
	    CWSetCReg(c, c->crm_sel0, c->crm_dir | c->crm_sel1);
	} else {
	    CWSetCReg(c, c->crm_sel1, c->crm_dir | c->crm_sel0);
	}

	for(j = 0; j < 86 && (inb(c->io_sr) & c->srm_trk0); j++) {
	    CWTriggerStep(c);
	    sleep_millis(6);
	}
	
	if(j < 86) {
	    c->drives[i].type = 1;
	    c->drives[i].track = 0;
	} else {
	    c->drives[i].type = 0;
	}
    }
    c->drives[0].sel = c->crm_sel0;
    c->drives[0].mot = c->crm_mot0;
    c->drives[1].sel = c->crm_sel1;
    c->drives[1].mot = c->crm_mot1;
    CWSetCReg(c, 0, c->crm_sel0 | c->crm_sel1); /* deselect all drives */
}

void catweasel_free_controller(catweasel_contr *c)
{
    if(!c->iobase)
	return;

    /* all motors off, deselect all drives */
    CWSetCReg(c, 0, c->crm_mot0 | c->crm_mot1 | c->crm_sel0 | c->crm_sel1);
}

void catweasel_set_motor(catweasel_drive *d, int on)
{
    CWSetCReg(d->contr, d->sel, 0);
    if (on)
	CWSetCReg(d->contr, d->mot, 0);
    else
	CWSetCReg(d->contr, 0, d->mot);
    CWSetCReg(d->contr, 0, d->sel);
}

int catweasel_step(catweasel_drive *d, int dir)
{
    catweasel_contr *c = d->contr;
    CWSetCReg(c, d->sel, 0);
    if (dir > 0)
	CWSetCReg(c, c->crm_dir, 0);
    else
	CWSetCReg(c, 0, c->crm_dir);
    CWTriggerStep (c);
    CWSetCReg(c, 0, d->sel);
    d->track += dir > 0 ? 1 : -1;
    return 1;
}

int catweasel_disk_changed(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_dchg) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

int catweasel_diskready(catweasel_drive	*d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_dskready) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

int catweasel_track0(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = (inb(d->contr->io_sr) & d->contr->srm_trk0) ? 0 : 1;
    CWSetCReg(d->contr, 0, d->sel);
    if (ret)
	d->track = 0;
    return ret;
}

int catweasel_write_protected(catweasel_drive *d)
{
    int ret;
    CWSetCReg(d->contr, d->sel, 0);
    ret = !(inb(d->contr->io_sr) & 8);
    CWSetCReg(d->contr, 0, d->sel);
    return ret;
}

uae_u8 catweasel_read_byte(catweasel_drive *d)
{
    return inb(d->contr->io_mem);
}

static const unsigned char amiga_thresholds[] = { 0x22, 0x30 }; // 27, 38 for 5.25"

#define FLOPPY_WRITE_LEN 6250

#define MFMMASK 0x55555555
static uae_u32 getmfmlong (uae_u16 * mbuf)
{
	return (uae_u32)(((*mbuf << 16) | *(mbuf + 1)) & MFMMASK);
}

static int drive_write_adf_amigados (uae_u16 *mbuf, uae_u16 *mend, uae_u8 *writebuffer, int track)
{
	int i, secwritten = 0;
	uae_u32 odd, even, chksum, id, dlong;
	uae_u8 *secdata;
	uae_u8 secbuf[544];
	char sectable[22];
	int num_sectors = 11;
	int ec = 0;

	memset (sectable, 0, sizeof (sectable));
	mend -= (4 + 16 + 8 + 512);
	while (secwritten < num_sectors) {
		int trackoffs;

		do {
			while (*mbuf++ != 0x4489) {
			    if (mbuf >= mend) {
				ec = 1;
				goto err;
			    }
			}
		} while (*mbuf++ != 0x4489);

		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		id = (odd << 1) | even;

		trackoffs = (id & 0xff00) >> 8;
		if (trackoffs > 10) {
		    ec = 2;
		    goto err;
		}
		chksum = odd ^ even;
		for (i = 0; i < 4; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 8);
			mbuf += 2;

			dlong = (odd << 1) | even;
			if (dlong) {
			    ec = 6;
			    goto err;
			}
			chksum ^= odd ^ even;
		} /* could check here if the label is nonstandard */
		mbuf += 8;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		if (((odd << 1) | even) != chksum) {
		    ec = 3;
		    goto err;
		}
		odd = (id & 0x00ff0000) >> 16;
		if (odd != track) {
		    ec = 7;
		    goto err;
		}
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		chksum = (odd << 1) | even;
		secdata = secbuf + 32;
		for (i = 0; i < 128; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 256);
			mbuf += 2;
			dlong = (odd << 1) | even;
			*secdata++ = dlong >> 24;
			*secdata++ = dlong >> 16;
			*secdata++ = dlong >> 8;
			*secdata++ = dlong;
			chksum ^= odd ^ even;
		}
		mbuf += 256;
		if (chksum) {
		    ec = 4;
		    goto err;
		}
		sectable[trackoffs] = 1;
		secwritten++;
		memcpy (writebuffer + trackoffs * 512, secbuf + 32, 512);
	}
	if (secwritten == 0 || secwritten < 0) {
	    ec = 5;
	    goto err;
	}
	return 0;
err:
	write_log ("mfm decode error %d. secwritten=%d\n", ec, secwritten);
	for (i = 0; i < num_sectors; i++)
	    write_log ("%d:%d ", i, sectable[i]);
	write_log ("\n");
	return ec;
}

static void mfmcode (uae_u16 * mfm, int words)
{
    uae_u32 lastword = 0;

    while (words--) {
	uae_u32 v = *mfm;
	uae_u32 lv = (lastword << 16) | v;
	uae_u32 nlv = 0x55555555 & ~lv;
	uae_u32 mfmbits = (nlv << 1) & (nlv >> 1);

	*mfm++ = v | mfmbits;
	lastword = v;
    }
}

#define	FLOPPY_GAP_LEN 360

static int amigados_mfmcode (uae_u8 *src, uae_u16 *dst, int num_secs, int track)
{
	int sec;
	memset (dst, 0xaa, FLOPPY_GAP_LEN * 2);

	for (sec = 0; sec < num_secs; sec++) {
	    uae_u8 secbuf[544];
	    int i;
	    uae_u16 *mfmbuf = dst + 544 * sec + FLOPPY_GAP_LEN;
	    uae_u32 deven, dodd;
	    uae_u32 hck = 0, dck = 0;

	    secbuf[0] = secbuf[1] = 0x00;
	    secbuf[2] = secbuf[3] = 0xa1;
	    secbuf[4] = 0xff;
	    secbuf[5] = track;
	    secbuf[6] = sec;
	    secbuf[7] = num_secs - sec;

	    for (i = 8; i < 24; i++)
		secbuf[i] = 0;

	    mfmbuf[0] = mfmbuf[1] = 0xaaaa;
	    mfmbuf[2] = mfmbuf[3] = 0x4489;

	    memcpy (secbuf + 32, src + sec * 512, 512);
	    deven = ((secbuf[4] << 24) | (secbuf[5] << 16)
		     | (secbuf[6] << 8) | (secbuf[7]));
	    dodd = deven >> 1;
	    deven &= 0x55555555;
	    dodd &= 0x55555555;

	    mfmbuf[4] = dodd >> 16;
	    mfmbuf[5] = dodd;
	    mfmbuf[6] = deven >> 16;
	    mfmbuf[7] = deven;

	    for (i = 8; i < 48; i++)
		mfmbuf[i] = 0xaaaa;
	    for (i = 0; i < 512; i += 4) {
		deven = ((secbuf[i + 32] << 24) | (secbuf[i + 33] << 16)
			 | (secbuf[i + 34] << 8) | (secbuf[i + 35]));
		dodd = deven >> 1;
		deven &= 0x55555555;
		dodd &= 0x55555555;
		mfmbuf[(i >> 1) + 32] = dodd >> 16;
		mfmbuf[(i >> 1) + 33] = dodd;
		mfmbuf[(i >> 1) + 256 + 32] = deven >> 16;
		mfmbuf[(i >> 1) + 256 + 33] = deven;
	    }

	    for (i = 4; i < 24; i += 2)
		hck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];

	    deven = dodd = hck;
	    dodd >>= 1;
	    mfmbuf[24] = dodd >> 16;
	    mfmbuf[25] = dodd;
	    mfmbuf[26] = deven >> 16;
	    mfmbuf[27] = deven;

	    for (i = 32; i < 544; i += 2)
		dck ^= (mfmbuf[i] << 16) | mfmbuf[i + 1];

	    deven = dodd = dck;
	    dodd >>= 1;
	    mfmbuf[28] = dodd >> 16;
	    mfmbuf[29] = dodd;
	    mfmbuf[30] = deven >> 16;
	    mfmbuf[31] = deven;
	    mfmcode (mfmbuf + 4, 544 - 4);

	}
    return (num_secs * 544 + FLOPPY_GAP_LEN) * 2 * 8;
}

static uae_u16 amigamfmbuffer[LONGEST_TRACK];
static uae_u8 amigabuffer[512*22];

/* search and align to 0x4489 WORDSYNC markers */
static int isamigatrack(uae_u8 *mfmdata, uae_u8 *mfmdatae, uae_u16 *mfmdst, int track)
{
	uae_u16 *dst = amigamfmbuffer;
	int len;
	int shift, syncshift, sync,ret;
	uae_u32 l;
	uae_u16 w;

	sync = syncshift = shift = 0;
	len = (mfmdatae - mfmdata) * 8;
	if (len > LONGEST_TRACK * 8)
	    len = LONGEST_TRACK * 8;
	while (len--) {
		l = (mfmdata[0] << 16) | (mfmdata[1] << 8) | (mfmdata[2] << 0);
		w = l >> (8 - shift);
		if (w == 0x4489) {
			sync = 1;
			syncshift = 0;
		}
		if (sync) {
			if (syncshift == 0) *dst++ = w;
			syncshift ++;
			if (syncshift == 16) syncshift = 0;
		}
		shift++;
		if (shift == 8) {
			mfmdata++;
			shift = 0;
		}
	}
	if (sync) {
		ret=drive_write_adf_amigados (amigamfmbuffer, dst, amigabuffer, track);
		if(!ret)
		    return amigados_mfmcode (amigabuffer, mfmdst, 11, track);
		write_log ("decode error %d\n", ret);
	} else {
	    write_log ("decode error: no sync found\n");
	}
	return 0;
}



int catweasel_fillmfm (catweasel_drive *d, uae_u16 *mfm, int side, int clock, int rawmode)
{
    int i, j, oldsync, syncs[10], synccnt, endcnt;
    uae_u32 tt1 = 0, tt2 = 0;
    uae_u8 *p1;
    int bytes = 0, bits = 0;
    static int lasttrack, trycnt;

    if (cwc.type == 0)
	return 0;
    if (d->contr->control_register & d->mot)
	return 0;
    if (!catweasel_read (d, side, 1, rawmode))
	return 0;
    if(d->contr->type == CATWEASEL_TYPE_MK1) {
	inb(d->contr->iobase + 1);
	inb(d->contr->io_mem); /* ignore first byte */
    } else {
	outb(0, d->contr->iobase + 0xe4);
    }
    catweasel_read_byte (d);
    if (lasttrack == d->track)
	trycnt++;
    else
	trycnt = 0;
    lasttrack = d->track;
    codec_init_threshtab(trycnt, amiga_thresholds);
    i = 0; j = 0;
    synccnt = 0;
    oldsync = -1;
    endcnt = 0;
    while (j < LONGEST_TRACK * 4) {
	uae_u8 b = catweasel_read_byte (d);
	if (b >= 250) {
	    if (b == 255 - endcnt) {
		endcnt++;
		if (endcnt == 5)
		    break;
	    } else
		endcnt = 0;
	}
	if (rawmode) {
	    if (b & 0x80) {
		if (oldsync < j) {
		    syncs[synccnt++] = j;
		    oldsync = j + 300;
		}
	    }
	    if (synccnt >= 3 && j > oldsync)
		break;
	}
	b = threshtab[b & 0x7f];
	tt1 = (tt1 << b) + 1;
	tt2 += b;

	if (tt2 >= 16) {
	    tt2 -= 16;
	    mfmbuf[j++] = tt1 >> (tt2 + 8);
	    mfmbuf[j++] = tt1 >> tt2;
	}
	i++;
    }
    write_log ("cyl=%d, side=%d, length %d, syncs %d\n", d->track, side, j, synccnt);
    if (rawmode) {
	if (synccnt >= 3) {
	    p1 = scantrack (mfmbuf + syncs[1], mfmbuf + syncs[2], &bytes, &bits);
	    if (p1) {
		j = 0;
		for (i = 0; i < bytes + 2; i+=2) {
		    mfm[j++] = (p1[i] << 8) | p1[i + 1];
		}
		return bytes * 8 + bits;
	    }
	}
    } else {
	return isamigatrack (mfmbuf, mfmbuf + j, mfm, d->track * 2 + side);
    }
    return 0;
}
	
int catweasel_read(catweasel_drive *d, int side, int clock, int rawmode)
{
    int iobase = d->contr->iobase;

    CWSetCReg(d->contr, d->sel, 0);
    if(d->contr->type == CATWEASEL_TYPE_MK1) {
	CWSetCReg(d->contr, 1<<2, (!side)<<2); /* set disk side */

	inb(iobase+1); /* ra reset */
	outb(clock*128, iobase+3);

	inb(iobase+1);
	inb(iobase+0);
//	inb(iobase+0);
//	outb(0, iobase+3); /* don't store index pulse */

	inb(iobase+1);

	inb(iobase+7); /* start reading */
	sleep_millis(rawmode ? 550 : 225);
	outb(0, iobase+1); /* stop reading, don't reset RAM pointer */

	outb(128, iobase+0); /* add data end mark */
	outb(128, iobase+0);

	inb(iobase+1); /* Reset RAM pointer */
    } else {
	CWSetCReg(d->contr, 1<<6, (!side)<<6); /* set disk side */

	outb(0, iobase + 0xe4); /* Reset memory pointer */
	switch(clock) {
	case 0: /* 28MHz */
	    outb(128, iobase + 0xec);
	    break;
	case 1: /* 14MHz */
	    outb(0, iobase + 0xec);
	    break;
	}
	inb(iobase + 0xe0);
	inb(iobase + 0xe0);
	outb(0, iobase + 0xec); /* no IRQs, no MFM predecode */
	inb(iobase + 0xe0);
	outb(0, iobase + 0xec); /* don't store index pulse */

	outb(0, iobase + 0xe4); /* Reset memory pointer */
	inb(iobase + 0xf0); /* start reading */
	sleep_millis(rawmode ? 550 : 225);
	inb(iobase + 0xe4); /* stop reading, don't reset RAM pointer */

	outb(255, iobase + 0xe0); /* add data end mark */
	outb(254, iobase + 0xe0); /* add data end mark */
	outb(253, iobase + 0xe0); /* add data end mark */
	outb(252, iobase + 0xe0); /* add data end mark */
	outb(251, iobase + 0xe0); /* add data end mark */
	outb(0, iobase + 0xe4); /* Reset memory pointer */
    }
    CWSetCReg(d->contr, 0, d->sel);
    return 1;
}

#endif


