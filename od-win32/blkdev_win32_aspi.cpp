/*
* UAE - The Un*x Amiga Emulator
*
* WIN32 CDROM/HD low level access code (ASPI)
*
* Copyright 2002 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"

#include "memory.h"

#include "threaddep/thread.h"
#include "blkdev.h"
#include "scsidev.h"
#include "gui.h"

#include <wnaspi32.h>

#include "scsidef.h"

int aspi_allow_misc = 1;
int aspi_allow_all = 0;

static int busses, AspiLoaded;
static DWORD (_cdecl *pfnGetASPI32SupportInfo)(void);
static DWORD (_cdecl *pfnSendASPI32Command)(LPSRB);
static BOOL (_cdecl *pfnGetASPI32Buffer)(PASPI32BUFF);
static BOOL (_cdecl *pfnFreeASPI32Buffer)(PASPI32BUFF);
static BOOL (_cdecl *pfnTranslateASPI32Address)(PDWORD, PDWORD);
static HMODULE hAspiLib;
static int scanphase;

struct scsi_info {
	int scsibus,target,lun;
	int type;
	int mediainserted;
	uae_u8 *buf;
	TCHAR label[100];
	SCSI *handle;
	int isatapi;
	int removable;
};
static struct scsi_info si[MAX_TOTAL_SCSI_DEVICES];
static int unitcnt;

static int getversion(const TCHAR *name, VS_FIXEDFILEINFO *ver)
{
	int ok = FALSE;
	DWORD  dwVersionHandle, dwFileVersionInfoSize;
	LPVOID lpFileVersionData = NULL;

	dwFileVersionInfoSize = GetFileVersionInfoSize(name, &dwVersionHandle);
	if (dwFileVersionInfoSize) {
		if (lpFileVersionData = xcalloc(uae_u8, dwFileVersionInfoSize)) {
			if (GetFileVersionInfo(name, dwVersionHandle, dwFileVersionInfoSize, lpFileVersionData)) {
				VS_FIXEDFILEINFO *vsFileInfo = NULL;
				UINT uLen;
				if (VerQueryValue(lpFileVersionData, TEXT("\\"), (void **)&vsFileInfo, &uLen)) {
					if(vsFileInfo) {
						memcpy (ver, vsFileInfo, sizeof (*ver));
						ok = TRUE;
						write_log (_T("%s version %d.%d.%d.%d\n"), name,
							vsFileInfo->dwFileVersionMS >> 16,
							vsFileInfo->dwFileVersionMS & 0xffff,
							vsFileInfo->dwFileVersionLS >> 16,
							vsFileInfo->dwFileVersionLS & 0xffff);
					}
				}
			}
			xfree(lpFileVersionData);
		}
	}
	return ok;
}

const TCHAR *get_aspi_path (int aspitype)
{
	static int nero, adaptec, frog;
	static TCHAR path_nero[MAX_DPATH];
	static TCHAR path_adaptec[MAX_DPATH];
	static const TCHAR *path_frog = _T("FrogAspi.dll");
	VS_FIXEDFILEINFO ver;

	switch (aspitype)
	{
	case 2: // Frog
		if (frog > 0)
			return path_frog;
		if (frog < 0)
			return NULL;
		frog = -1;
		if (getversion(path_frog, &ver))
			frog = 1;
		return path_frog;

	case 1: // Nero
		{
			HKEY key;
			DWORD type = REG_SZ;
			DWORD size = sizeof (path_nero);
			if (nero > 0)
				return path_nero;
			if (nero < 0)
				return NULL;
			nero = -1;
			if (RegOpenKeyEx (HKEY_LOCAL_MACHINE, _T("SOFTWARE\\Ahead\\shared"), 0, KEY_READ, &key) == ERROR_SUCCESS) {
				if (RegQueryValueEx (key, _T("NeroAPI"), 0, &type, (LPBYTE)path_nero, &size) == ERROR_SUCCESS) {
					if (path_nero[_tcslen (path_nero) - 1] != '\\')
						_tcscat (path_nero, _T("\\"));
					_tcscat (path_nero, _T("wnaspi32.dll"));
					RegCloseKey (key);
					if (getversion(path_nero, &ver)) {
						if (ver.dwFileVersionMS >= 0x20000) {
							nero = 1;
							return path_nero;
						}
					}
					return NULL;
				}
				RegCloseKey (key);
			}
		}
		return NULL;

	case 0: // Adaptec
		{
			if (adaptec > 0)
				return path_adaptec;
			if (adaptec < 0)
				return NULL;
			adaptec = -1;
			_tcscpy (path_adaptec, _T("wnaspi32.dll"));
			if (getversion(path_adaptec, &ver)) {
				if (ver.dwFileVersionMS >= 0x40000 || ver.dwFileVersionMS < 0x10000) {
					adaptec = 1;
					return path_adaptec;
				}
			}
		}
		return NULL;
	}
	return NULL;
}

static int ha_inquiry (SCSI *scgp, int id, SRB_HAInquiry *ip)
{
	DWORD Status;

	ip->SRB_Cmd = SC_HA_INQUIRY;
	ip->SRB_HaId = id;
	ip->SRB_Flags = 0;
	ip->SRB_Hdr_Rsvd = 0;

	Status = pfnSendASPI32Command((LPSRB)ip);
	if (log_scsi) {
		TCHAR *s1 = au ((char*)ip->HA_ManagerId);
		TCHAR *s2 = au ((char*)ip->HA_Identifier);
		write_log (_T("ASPI: S=%d ha=%d, ID=%d, M='%s', Id='%s'\n"),
			Status, ip->HA_Count, ip->HA_SCSI_ID, s1, s2);
		xfree (s2);
		xfree (s1);
	}
	if (ip->SRB_Status != SS_COMP)
		return -1;
	return 0;
}

static int open_driver (SCSI *scgp)
{
	TCHAR path[MAX_DPATH];
	DWORD astatus;
	BYTE HACount;
	BYTE ASPIStatus;
	int i;
	int nero, frog;

	/*
	* Check if ASPI library is already loaded
	*/
	if (AspiLoaded == TRUE)
		return TRUE;

	nero = frog = 0;
	_tcscpy (path, _T("WNASPI32"));
	if (currprefs.win32_uaescsimode == UAESCSI_NEROASPI) {
		const TCHAR *p = get_aspi_path (1);
		if (p) {
			_tcscpy (path, p);
			nero = 1;
		}
	} else if (currprefs.win32_uaescsimode == UAESCSI_FROGASPI) {
		const TCHAR *p = get_aspi_path (2);
		if (p) {
			_tcscpy (path, p);
			frog = 1;
		}
	}
	/*
	* Load the ASPI library
	*/
	write_log (_T("ASPI: driver location '%s'\n"), path);
	hAspiLib = LoadLibrary (path);
	if (hAspiLib == NULL && (nero || frog)) {
		write_log (_T("ASPI: NERO/FROG ASPI failed to load, falling back to default\n"));
		hAspiLib = LoadLibrary (_T("WNASPI32"));
	}

	/*
	* Check if ASPI library is loaded correctly
	*/
	if (hAspiLib == NULL) {
		write_log (_T("ASPI: failed to load wnaspi32.dll\n"));
		return FALSE;
	}
	/*
	* Get a pointer to GetASPI32SupportInfo function
	* and a pointer to SendASPI32Command function
	*/
	pfnGetASPI32SupportInfo = (DWORD(_cdecl *)(void))GetProcAddress (hAspiLib, "GetASPI32SupportInfo");
	pfnSendASPI32Command = (DWORD(_cdecl *)(LPSRB))GetProcAddress (hAspiLib, "SendASPI32Command");

	if (pfnGetASPI32SupportInfo == NULL || pfnSendASPI32Command == NULL) {
		write_log (_T("ASPI: obsolete wnaspi32.dll found\n"));
		return FALSE;
	}

	pfnGetASPI32Buffer = (BOOL(_cdecl *)(PASPI32BUFF))GetProcAddress (hAspiLib, "GetASPI32Buffer");
	pfnFreeASPI32Buffer = (BOOL(_cdecl *)(PASPI32BUFF))GetProcAddress (hAspiLib, "FreeASPI32Buffer");
	pfnTranslateASPI32Address = (BOOL(_cdecl *)(PDWORD, PDWORD))GetProcAddress (hAspiLib, "TranslateASPI32Address");

	/*
	* Set AspiLoaded variable
	*/
	AspiLoaded = TRUE;

	astatus = pfnGetASPI32SupportInfo();

	ASPIStatus = HIBYTE(LOWORD(astatus));
	HACount    = LOBYTE(LOWORD(astatus));

	write_log (_T("ASPI: open_driver %X HostASPIStatus=0x%x HACount=0x%x\n"), astatus, ASPIStatus, HACount);

	if (ASPIStatus != SS_COMP && ASPIStatus != SS_NO_ADAPTERS) {
		write_log (_T("ASPI: Could not find any host adapters, ASPIStatus == 0x%02X\n"), ASPIStatus);
		return FALSE;
	}
	busses = HACount;

	write_log (_T("ASPI: open_driver HostASPIStatus=0x%x HACount=0x%x\n"), ASPIStatus, HACount);

	if (!scgp)
		return TRUE;

	for (i=0; i < busses; i++) {
		SRB_HAInquiry s;
		ha_inquiry(scgp, i, &s);
	}
	return TRUE;
}


static void close_driver (void)
{
	if (!AspiLoaded)
		return;
	AspiLoaded = FALSE;
	pfnGetASPI32SupportInfo = NULL;
	pfnSendASPI32Command = NULL;
	pfnGetASPI32Buffer = NULL;
	pfnFreeASPI32Buffer = NULL;
	pfnTranslateASPI32Address = NULL;
	FreeLibrary(hAspiLib);
	hAspiLib = NULL;
}


static int scsi_bufsize (SCSI *scgp, int amt)
{
	return 63*1024;
}

static int scsi_havebus (SCSI *scgp, int busno)
{
	if (busno < 0 || busno >= busses)
		return FALSE;
	return TRUE;
}

static void *scgo_getbuf (SCSI *scgp, long amt)
{
	scgp->bufbase = malloc((size_t)(amt));
	return scgp->bufbase;
}

static void scsi_sfree (SCSI *scgp)
{
	if (!scgp)
		return;
	if (scgp->cmdstart)
		free(scgp->cmdstart);
	if (scgp->cmdstop)
		free(scgp->cmdstop);
	if (scgp->scmd)
		free(scgp->scmd);
	if (scgp->inq)
		free(scgp->inq);
	if (scgp->cap)
		free(scgp->cap);
	if (scgp->local)
		free(scgp->local);
	if (scgp->errstr)
		free(scgp->errstr);
	free(scgp);
}

static SCSI *scsi_smalloc(void)
{
	SCSI *scgp;

	scgp = (SCSI *)malloc(sizeof(*scgp));
	if (scgp == NULL)
		return 0;

	memset (scgp, 0, sizeof (*scgp));
	scgp->deftimeout = 20;
	scgp->running = FALSE;

	scgp->cmdstart = (struct timeval *)malloc(sizeof(struct timeval));
	if (scgp->cmdstart == NULL)
		goto err;
	scgp->cmdstop = (struct timeval *)malloc(sizeof(struct timeval));
	if (scgp->cmdstop == NULL)
		goto err;
	scgp->scmd = (struct scg_cmd *)malloc(sizeof(struct scg_cmd));
	if (scgp->scmd == NULL)
		goto err;
	scgp->errstr = xmalloc(char, SCSI_ERRSTR_SIZE);
	if (scgp->errstr == NULL)
		goto err;
	scgp->errptr = scgp->errbeg = scgp->errstr;
	scgp->errstr[0] = '\0';
	scgp->inq = (struct scsi_inquiry *)malloc(sizeof(struct scsi_inquiry));
	if (scgp->inq == NULL)
		goto err;
	scgp->cap = (struct scsi_capacity *)malloc(sizeof(struct scsi_capacity));
	if (scgp->cap == NULL)
		goto err;
	return scgp;
err:
	scsi_sfree(scgp);
	return 0;
}

#define MAX_SCG 16 /* Max # of SCSI controllers */
#define MAX_TGT 16 /* Max # of SCSI Targets */
#define MAX_LUN 8  /* Max # of SCSI LUNs */

struct scg_local {
	int dummy;
};
#define scglocal(p) ((struct scg_local *)((p)->local))

static SCSI *openscsi (int busno, int tgt, int tlun)
{
	SCSI *scgp = scsi_smalloc ();

	if (busno >= MAX_SCG || tgt >= MAX_TGT || tlun >= MAX_LUN) {
		errno = EINVAL;
		if (log_scsi)
			write_log (_T("ASPI: Illegal value for busno, target or lun '%d,%d,%d'\n"), busno, tgt, tlun);
		scsi_sfree (scgp);
		return 0;
	}
	/*
	*  Check if variables are within the range
	*/
	if (tgt >= 0 && tgt >= 0 && tlun >= 0) {
		/*
		* This is the non -scanbus case.
		*/
		;
	} else if (tgt != -1 || tgt != -1 || tlun != -1) {
		errno = EINVAL;
		scsi_sfree (scgp);
		return 0;
	}
	if (scgp->local == NULL) {
		scgp->local = malloc(sizeof(struct scg_local));
		if (scgp->local == NULL) {
			scsi_sfree (scgp);
			return 0;
		}
	}
	/*
	* Try to open ASPI-Router
	*/
	if (!open_driver(scgp)) {
		scsi_sfree (scgp);
		return 0;
	}
	/*
	* More than we have ...
	*/
	if (busno >= busses) {
		close_driver ();
		scsi_sfree (scgp);
		return 0;
	}
	return scgp;
}

static void closescsi (SCSI *scgp)
{
	close_driver ();
}

static void scsi_debug (SCSI *scgp, SRB_ExecSCSICmd *s)
{
	if (!log_scsi)
		return;
	if (scanphase)
		return;
	write_log (_T("ASPI EXEC_SCSI: bus=%d,target=%d,lun=%d\n"),
		s->SRB_HaId, s->SRB_Target, s->SRB_Lun);
	scsi_log_before (scgp->scmd->cdb.cmd_cdb, scgp->scmd->cdb_len,
		(s->SRB_Flags & SRB_DIR_OUT) ? s->SRB_BufPointer : 0, s->SRB_BufLen);
}


static void copy_sensedata(SRB_ExecSCSICmd *cp,	struct scg_cmd *sp)
{
	sp->sense_count = cp->SRB_SenseLen;
	if (sp->sense_count > sp->sense_len)
		sp->sense_count = sp->sense_len;
	memset(&sp->u_sense.Sense, 0x00, sizeof(sp->u_sense.Sense));
	if(sp->sense_len > 0) {
		int len = sp->sense_len;
		if (len > sizeof(sp->u_sense.Sense)) len = sizeof(sp->u_sense.Sense);
		memcpy(&sp->u_sense.Sense, cp->SenseArea, len);
	}
	sp->u_scb.cmd_scb[0] = cp->SRB_TargStat;
}

/*
* Set error flags
*/
static void set_error(SRB_ExecSCSICmd *cp, struct scg_cmd *sp)
{
	switch (cp->SRB_Status) {
	case SS_COMP:			/* 0x01 SRB completed without error  */
		sp->error = SCG_NO_ERROR;
		sp->ux_errno = 0;
		break;
	case SS_PENDING:		/* 0x00 SRB being processed          */
		/*
		* XXX Could SS_PENDING happen ???
		*/
	case SS_ABORTED:		/* 0x02 SRB aborted                  */
	case SS_ABORT_FAIL:		/* 0x03 Unable to abort SRB          */
	case SS_ERR:			/* 0x04 SRB completed with error     */
	default:
		sp->error = SCG_RETRYABLE;
		sp->ux_errno = EIO;
		break;
	case SS_INVALID_CMD:		/* 0x80 Invalid ASPI command         */
	case SS_INVALID_HA:		/* 0x81 Invalid host adapter number  */
	case SS_NO_DEVICE:		/* 0x82 SCSI device not installed    */
	case SS_INVALID_SRB:		/* 0xE0 Invalid parameter set in SRB */
	case SS_ILLEGAL_MODE:		/* 0xE2 Unsupported Windows mode     */
	case SS_NO_ASPI:		/* 0xE3 No ASPI managers             */
	case SS_FAILED_INIT:		/* 0xE4 ASPI for windows failed init */
	case SS_MISMATCHED_COMPONENTS:	/* 0xE7 The DLLs/EXEs of ASPI don't  */
		/*      version check                */
	case SS_NO_ADAPTERS:		/* 0xE8 No host adapters to manager  */

	case SS_ASPI_IS_SHUTDOWN:	/* 0xEA Call came to ASPI after      */
		/*      PROCESS_DETACH               */
	case SS_BAD_INSTALL:		/* 0xEB The DLL or other components  */
		/*      are installed wrong          */
		sp->error = SCG_FATAL;
		sp->ux_errno = EINVAL;
		break;
	case SS_BUFFER_ALIGN:		/* 0xE1 Buffer not aligned (replaces */
		/*      SS_OLD_MANAGER in Win32)     */
		sp->error = SCG_FATAL;
		sp->ux_errno = EFAULT;
		break;

	case SS_ASPI_IS_BUSY:		/* 0xE5 No resources available to    */
		/*      execute command              */
		sp->error = SCG_RETRYABLE;
		sp->ux_errno = EBUSY;
		break;

	case SS_BUFFER_TO_BIG:		/* 0xE6 Correct spelling of 'too'    */
	case SS_INSUFFICIENT_RESOURCES:	/* 0xE9 Couldn't allocate resources  */
		/*      needed to init               */
		sp->error = SCG_RETRYABLE;
		sp->ux_errno = ENOMEM;
		break;
	}
}
static int scsiabort(SCSI *scgp, SRB_ExecSCSICmd *sp)
{
	DWORD Status = 0;
	SRB_Abort s;

	if (log_scsi)
		write_log (_T("ASPI: Attempting to abort SCSI command\n"));
	/*
	* Set structure variables
	*/
	s.SRB_Cmd	= SC_ABORT_SRB;			/* ASPI command code = SC_ABORT_SRB	*/
	s.SRB_HaId	= scg_scsibus(scgp);		/* ASPI host adapter number		*/
	s.SRB_Flags	= 0;				/* Flags				*/
	s.SRB_ToAbort	= (LPSRB)&sp;		/* sp					*/
	/*
	* Initiate SCSI abort
	*/
	Status = pfnSendASPI32Command((LPSRB)&s);
	/*
	* Check condition
	*/
	if (s.SRB_Status != SS_COMP) {
		if (log_scsi)
			write_log (_T("ASPI: Abort ERROR! 0x%08X\n"), s.SRB_Status);
		return FALSE;
	}
	if (log_scsi)
		write_log (_T("ASPI: Abort SCSI command completed\n"));
	/*
	* Everything went OK
	*/
	return TRUE;
}

static int scsicmd(SCSI *scgp)
{
	struct scg_cmd *sp = scgp->scmd;
	DWORD Status = 0;
	DWORD EventStatus = WAIT_OBJECT_0;
	HANDLE Event = NULL;
	SRB_ExecSCSICmd s;

	/*
	* Initialize variables
	*/
	sp->error = SCG_NO_ERROR;
	sp->sense_count = 0;
	sp->u_scb.cmd_scb[0] = 0;
	sp->resid = 0;

	memset(&s, 0, sizeof(s)); /* Clear SRB structure */

	/*
	* Check cbd_len > the maximum command pakket that can be handled by ASPI
	*/
	if (sp->cdb_len > 16) {
		sp->error = SCG_FATAL;
		sp->ux_errno = EINVAL;
		if (log_scsi)
			write_log (_T("ASPI: sp->cdb_len > sizeof(SRB_ExecSCSICmd.CDBByte). Fatal error in scgo_send, exiting...\n"));
		return -1;
	}
	/*
	* copy command into SRB
	*/
	memcpy(&(s.CDBByte), &sp->cdb, sp->cdb_len);

	Event = CreateEvent(NULL, TRUE, FALSE, NULL);

	/*
	* Fill ASPI structure
	*/
	s.SRB_Cmd = SC_EXEC_SCSI_CMD; /* SCSI Command */
	s.SRB_HaId = scg_scsibus(scgp); /* Host adapter number */
	s.SRB_Flags = SRB_EVENT_NOTIFY; /* Flags */
	s.SRB_Target = scg_target(scgp); /* Target SCSI ID */
	s.SRB_Lun = scg_lun(scgp); /* Target SCSI LUN */
	s.SRB_BufLen = sp->size; /* # of bytes transferred */
	s.SRB_BufPointer= (BYTE*)sp->addr; /* pointer to data buffer */
	s.SRB_CDBLen = sp->cdb_len;  /* SCSI command length */
	s.SRB_PostProc = Event; /* Post proc event */
	s.SRB_SenseLen = SENSE_LEN; /* Lenght of sense buffer */

	/*
	* Do we receive data from this ASPI command?
	*/
	if (sp->flags & SCG_RECV_DATA) {
		s.SRB_Flags |= SRB_DIR_IN;
	} else {
		/*
		* Set direction to output
		*/
		if (sp->size > 0)
			s.SRB_Flags |= SRB_DIR_OUT;
	}

	scsi_debug (scgp,&s);

	/*
	* ------------ Send SCSI command --------------------------
	*/

	ResetEvent (Event); /* Clear event handle */
	Status = pfnSendASPI32Command((LPSRB)&s);/* Initiate SCSI command  */

	if (Status == SS_PENDING) { /* If in progress */
		/*
		* Wait until command completes, or times out.
		*/
		EventStatus = WaitForSingleObject(Event, sp->timeout * 1000);
		if (EventStatus == WAIT_OBJECT_0)
			ResetEvent(Event); /* Clear event, time out */
		if (s.SRB_Status == SS_PENDING) {/* Check if we got a timeout*/
			scsiabort(scgp, &s);
			ResetEvent(Event); /* Clear event, time out */
			CloseHandle(Event); /* Close the event handle */
			sp->error = SCG_TIMEOUT;
			return 1; /* Return error */
		}
	}
	CloseHandle (Event); /* Close the event handle  */

	/*
	* Check ASPI command status
	*/

	if (log_scsi && !scanphase)
		scsi_log_after ((s.SRB_Flags & SRB_DIR_IN) ? s.SRB_BufPointer : 0, s.SRB_BufLen,
		sp->u_sense.cmd_sense, sp->sense_len);

	if (s.SRB_Status != SS_COMP) {
		if (log_scsi && s.SRB_Status != 0x82)
			write_log (_T("ASPI: Error in scgo_send: s.SRB_Status is 0x%x\n"), s.SRB_Status);
		set_error(&s, sp); /* Set error flags */
		copy_sensedata(&s, sp); /* Copy sense and status */
		if (log_scsi && s.SRB_Status != 0x82)
			write_log (_T("ASPI: Mapped to: error %d errno: %d\n"), sp->error, sp->ux_errno);
		return 1;
	}
	/*
	* Return success
	*/
	return 0;
}

static int inquiry (SCSI *scgp, void *bp, int cnt)
{
	struct scg_cmd *scmd = scgp->scmd;

	memset(bp, 0, cnt);
	memset(scmd, 0, sizeof(struct scg_cmd));
	scmd->addr = bp;
	scmd->size = cnt;
	scmd->flags = SCG_RECV_DATA|SCG_DISRE_ENA;
	scmd->cdb_len = SC_G0_CDBLEN;
	scmd->sense_len = CCS_SENSE_LEN;
	scmd->target = scgp->addr.target;
	scmd->cdb.g0_cdb.cmd = SC_INQUIRY;
	scmd->cdb.g0_cdb.lun = scgp->addr.lun;
	scmd->cdb.g0_cdb.count = cnt;
	scgp->scmd->timeout = 10 * 60;
	if (scsicmd(scgp))
		return -1;
	return 0;
}

static int scsierr(SCSI *scgp)
{
	register struct scg_cmd *cp = scgp->scmd;

	if(cp->error != SCG_NO_ERROR ||
		cp->ux_errno != 0 || *(u_char*)&cp->scb != 0)
		return -1;
	return 0;
}

static void scan_scsi_bus (SCSI *scgp, int flags)
{
	/* add all units we find */
	write_log (_T("ASPI: SCSI scan starting..\n"));
	scanphase = 1;
	for (scgp->addr.scsibus=0; scgp->addr.scsibus < 8; scgp->addr.scsibus++) {
		if (!scsi_havebus(scgp, scgp->addr.scsibus))
			continue;
		for (scgp->addr.target=0; scgp->addr.target < 16; scgp->addr.target++) {
			struct scsi_inquiry inq;
			scgp->addr.lun = 0;
			if (inquiry (scgp, &inq, sizeof(inq)))
				continue;
			for (scgp->addr.lun=0; scgp->addr.lun < 8; scgp->addr.lun++) {
				if (!inquiry (scgp, &inq, sizeof(inq))) {
					TCHAR *vend, *prod, *rev;
					char tmp[20];

					memcpy (tmp, inq.vendor_info, 8);
					tmp[8] = 0;
					vend = au (tmp);
					memcpy (tmp, inq.prod_ident, 16);
					tmp[16] = 0;
					prod = au (tmp);
					memcpy (tmp, inq.prod_revision, 4);
					tmp[4] = 0;
					rev = au (tmp);

					write_log (_T("ASPI: %d:%d:%d '%s' '%s' '%s' "),
						scgp->addr.scsibus,scgp->addr.target,scgp->addr.lun, vend, prod, rev);
					if (unitcnt < MAX_TOTAL_SCSI_DEVICES) {
						struct scsi_info *cis = &si[unitcnt];
						int use = 0;
						write_log (_T("["));
						if (inq.type == INQ_ROMD) {
							write_log (_T("CDROM"));
							use = 1;
						} else if ((inq.type >= INQ_SEQD && inq.type < INQ_COMM && aspi_allow_misc) || aspi_allow_all) {
							write_log (_T("%d"), inq.type);
							use = 1;
						} else {
							write_log (_T("<%d>"), inq.type);
						}
						if (inq.ansi_version == 0) {
							write_log (_T(",ATAPI"));
							cis->isatapi = 1;
						} else
							write_log (_T(",SCSI"));
						write_log (_T("]"));
						if (use) {
							unitcnt++;
							cis->buf = xmalloc (uae_u8, DEVICE_SCSI_BUFSIZE);
							cis->scsibus = scgp->addr.scsibus;
							cis->target = scgp->addr.target;
							cis->lun = scgp->addr.lun;
							cis->type = inq.type;
							cis->removable = inq.removable;
							_stprintf (cis->label, _T("%s %s %s"), vend, prod, rev);
						}
					}
					write_log (_T("\n"));

					xfree (rev);
					xfree (prod);
					xfree (vend);
				}
			}
		}
	}
	write_log (_T("ASPI: SCSI scan ended\n"));
	scanphase = 0;
}

static void aspi_led (int unitnum)
{
	int type = si[unitnum].type;

	if (type == INQ_ROMD)
		gui_flicker_led (LED_CD, unitnum, 1);
	else if (type == INQ_DASD)
		gui_flicker_led (LED_HD, unitnum, 1);
}

static uae_sem_t scgp_sem;

static uae_u8 *execscsicmd_out (int unitnum, uae_u8 *data, int len)
{
	SCSI *scgp = si[unitnum].handle;
	int v;

	if (!scgp)
		return NULL;
	uae_sem_wait (&scgp_sem);
	memset(scgp->scmd, 0, sizeof(struct scg_cmd));
	scgp->scmd->cdb_len = len;
	memcpy (scgp->scmd->cdb.cmd_cdb, data, len);
	scgp->scmd->addr = 0;
	scgp->scmd->size = 0;
	scgp->addr.scsibus = si[unitnum].scsibus;
	scgp->addr.target = si[unitnum].target;
	scgp->addr.lun = si[unitnum].lun;
	scgp->scmd->timeout = 80 * 60;
	scgp->scmd->sense_len = CCS_SENSE_LEN;
	aspi_led (unitnum);
	v = scsicmd (scgp);
	aspi_led (unitnum);
	uae_sem_post (&scgp_sem);
	if (v)
		return 0;
	return data;
}

static uae_u8 *execscsicmd_in (int unitnum, uae_u8 *data, int len, int *outlen)
{
	SCSI *scgp = si[unitnum].handle;
	int v;

	if (!scgp)
		return NULL;
	uae_sem_wait (&scgp_sem);
	memset(scgp->scmd, 0, sizeof(struct scg_cmd));
	scgp->scmd->cdb_len = len;
	memcpy (scgp->scmd->cdb.cmd_cdb, data, len);
	scgp->scmd->addr = si[unitnum].buf;
	scgp->scmd->size = DEVICE_SCSI_BUFSIZE;
	scgp->addr.scsibus = si[unitnum].scsibus;
	scgp->addr.target = si[unitnum].target;
	scgp->addr.lun = si[unitnum].lun;
	scgp->scmd->timeout = 80 * 60;
	scgp->scmd->flags = SCG_RECV_DATA;
	scgp->scmd->sense_len = CCS_SENSE_LEN;
	aspi_led (unitnum);
	v = scsicmd (scgp);
	aspi_led (unitnum);
	uae_sem_post (&scgp_sem);
	if (v)
		return NULL;
	if (outlen)
		*outlen = scgp->scmd->size;
	return si[unitnum].buf;
}

static SCSI *scsi_handle;

static int open_scsi_bus (int flags)
{
	SCSI *scgp = openscsi (-1, -1, -1);
	if (scgp) {
		unitcnt = 0;
		scan_scsi_bus (scgp, flags);
		uae_sem_init (&scgp_sem, 0, 1);
		scsi_handle = scgp;
	}
	return scgp ? 1 : 0;
}

static void close_scsi_bus (void)
{
	closescsi (scsi_handle);
	scsi_handle = 0;
}

static int mediacheck (int unitnum)
{
	uae_u8 cmd [6] = { 0,0,0,0,0,0 }; /* TEST UNIT READY */
	if (si[unitnum].handle == 0)
		return 0;
	return execscsicmd_out(unitnum, cmd, sizeof(cmd)) ? 1 : 0;
}

static int mediacheck_full (int unitnum, struct device_info *di)
{
	uae_u8 cmd1[10] = { 0x25,0,0,0,0,0,0,0,0,0 }; /* READ CAPACITY */
	int ok, outlen;
	uae_u8 *p = si[unitnum].buf;

	di->sectorspertrack = 0;
	di->trackspercylinder = 0;
	di->bytespersector = 0;
	di->cylinders = 0;
	di->write_protected = 1;
	if (si[unitnum].handle == 0)
		return 0;
	ok = execscsicmd_in(unitnum, cmd1, sizeof cmd1, &outlen) ? 1 : 0;
	if (ok) {
		di->bytespersector = (p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
		di->sectorspertrack = ((p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]) + 1;
		di->trackspercylinder = 1;
		di->cylinders = 1;
	}
	if (di->type == INQ_DASD) {
		uae_u8 cmd2[10] = { 0x5a,0x08,0,0,0,0,0,0,0x10,0 }; /* MODE SENSE */
		ok = execscsicmd_in(unitnum, cmd2, sizeof cmd2, &outlen) ? 1 : 0;
		if (ok) {
			di->write_protected = (p[3] & 0x80) ? 1 : 0;
		}
	}
	return 1;
}

static int open_scsi_device (int unitnum, const TCHAR *ident, int flags)
{
	if (unitnum >= unitcnt)
		return 0;
	if (log_scsi)
		write_log (_T("ASPI: opening %d:%d:%d (%d)\n"), si[unitnum].scsibus, si[unitnum].target, si[unitnum].lun, unitnum);
	si[unitnum].handle = openscsi (si[unitnum].scsibus, si[unitnum].target, si[unitnum].lun);
	if (si[unitnum].handle)
		si[unitnum].mediainserted = mediacheck (unitnum);
	if (log_scsi)
		write_log (_T("unit %d: %s\n"), unitnum, si[unitnum].mediainserted ? _T("CD inserted") : _T("Drive empty"));
	return si[unitnum].handle ? 1 : 0;
}

static void close_scsi_device (int unitnum)
{
	if (unitnum >= unitcnt)
		return;
	if (!si[unitnum].handle)
		return;
	scsi_sfree (si[unitnum].handle);
	si[unitnum].handle = 0;
}

static int execscsicmd_direct (int unitnum, struct amigascsi *as)
{
	int sactual = 0, i, parm;
	SCSI *scgp = si[unitnum].handle;
	struct scg_cmd *scmd = scgp->scmd;
	int scsi_cmd_len_org = as->cmd_len;
	int io_error = 0;
	uae_u8 *scsi_datap, *scsi_datap_org;

	uae_sem_wait (&scgp_sem);

	/* the Amiga does not tell us how long the timeout shall be, so make it _very_ long (specified in seconds) */
	scmd->timeout = 80 * 60;
	scsi_datap = scsi_datap_org = as->len ? as->data : 0;
	scmd->flags = ((as->flags & 1) ? SCG_RECV_DATA : 0) | SCG_DISRE_ENA;
	for (i = 0; i < as->cmd_len; i++)
		scmd->cdb.cmd_cdb[i] = as->cmd[i];
	scmd->target = si[unitnum].target;
	scmd->sense_len = (as->flags & 4) ? 4 : /* SCSIF_OLDAUTOSENSE */
		(as->flags & 2) ? as->sense_len : /* SCSIF_AUTOSENSE */
		-1;
	scmd->sense_count = 0;
	scmd->u_scb.cmd_scb[0] = 0;
	scgp->addr.scsibus = si[unitnum].scsibus;
	scgp->addr.target = si[unitnum].target;
	scgp->addr.lun = si[unitnum].lun;
	if (si[unitnum].isatapi)
		scsi_atapi_fixup_pre (scmd->cdb.cmd_cdb, &as->cmd_len, &scsi_datap, &as->len, &parm);
	scmd->addr = scsi_datap;
	scmd->size = as->len;
	scmd->cdb_len = as->cmd_len;
	aspi_led (unitnum);
	scsicmd (scgp);
	aspi_led (unitnum);

	as->cmdactual = scmd->error == SCG_FATAL ? 0 : scsi_cmd_len_org; /* fake scsi_CmdActual */
	as->status = scmd->u_scb.cmd_scb[0]; /* scsi_Status */
	if (scmd->u_scb.cmd_scb[0]) {
		io_error = 45; /* HFERR_BadStatus */
		/* copy sense? */
		for (sactual = 0; sactual < as->sense_len && sactual < scmd->sense_count; sactual++)
			as->sensedata[sactual] = scmd->u_sense.cmd_sense[sactual];
		as->actual = 0; /* scsi_Actual */
	} else {
		int i;
		for (i = 0; i < as->sense_len; i++)
			as->sensedata[i] = 0;
		sactual = 0;
		if (scmd->error != SCG_NO_ERROR) {
			io_error = 20; /* io_Error, but not specified */
			as->actual = 0; /* scsi_Actual */
		} else {
			io_error = 0;
			if (si[unitnum].isatapi)
				scsi_atapi_fixup_post (scmd->cdb.cmd_cdb, as->cmd_len, scsi_datap_org, scsi_datap, &as->len, parm);
			as->actual = as->len - scmd->resid; /* scsi_Actual */
		}
	}
	as->sactual = sactual;

	uae_sem_post (&scgp_sem);

	if (scsi_datap != scsi_datap_org)
		free (scsi_datap);

	return io_error;
}

static struct device_info *info_device (int unitnum, struct device_info *di, int quick)
{
	struct scsi_info *sif = &si[unitnum];
	if (unitnum >= unitcnt)
		return 0;
	di->open = sif->handle != 0;
	di->bus = sif->scsibus;
	di->target = sif->target;
	di->lun = sif->lun;
	di->media_inserted = mediacheck (unitnum);
	di->type = sif->type;
	mediacheck_full (unitnum, di);
	di->unitnum = unitnum + 1;
	di->removable = sif->removable;
	_tcscpy (di->label, sif->label);
	di->backend = _T("ASPI");
	if (log_scsi) {
		write_log (_T("MI=%d TP=%d WP=%d CY=%d BK=%d '%s'\n"),
			di->media_inserted, di->type, di->write_protected, di->cylinders, di->bytespersector, di->label);
	}
	return di;
}

bool win32_aspi_media_change (TCHAR driveletter, int insert)
{
	int i, now;

	for (i = 0; i < unitcnt; i++) {
		if (si[i].type == INQ_ROMD) {
			now = mediacheck (i);
			if (now != si[i].mediainserted) {
				write_log (_T("ASPI: media change %c %d\n"), driveletter, insert);
				si[i].mediainserted = now;
				scsi_do_disk_change (i + 1, insert, NULL);
				return true;
			}
		}
	}
	return false;
}

static int check_isatapi (int unitnum)
{
	return si[unitnum].isatapi;
}

struct device_functions devicefunc_win32_aspi = {
	_T("ASPI"),
	open_scsi_bus, close_scsi_bus, open_scsi_device, close_scsi_device, info_device,
	execscsicmd_out, execscsicmd_in, execscsicmd_direct,
	0, 0, 0, 0, 0, 0, 0, 0, 0, check_isatapi, 0, 0
};
