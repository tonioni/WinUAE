/*
* UAE - The Un*x Amiga Emulator
*
* Win32 uaenet emulation
*
* Copyright 2007 Toni Wilen
*/

#include "sysconfig.h"

#ifdef WITH_SLIRP
#include "../slirp/slirp.h"
#endif

#include <stdio.h>

#define HAVE_REMOTE
#define WPCAP
#include "pcap.h"

#include <windows.h>

#include "packet32.h"
#include "ntddndis.h"

#include "sysdeps.h"
#include "options.h"
#include "sana2.h"

#include "threaddep/thread.h"
#include "win32_uaenet.h"
#include "win32.h"

static struct netdriverdata tds[MAX_TOTAL_NET_DEVICES];
static int enumerated;

struct uaenetdatawin32
{
	HANDLE evttw;
	void *readdata, *writedata;

	uae_sem_t change_sem;

	volatile int threadactiver;
	uae_thread_id tidr;
	uae_sem_t sync_semr;
	volatile int threadactivew;
	uae_thread_id tidw;
	uae_sem_t sync_semw;

	void *user;
	struct netdriverdata *tc;
	uae_u8 *readbuffer;
	uae_u8 *writebuffer;
	int mtu;

	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *fp;
	uaenet_gotfunc *gotfunc;
	uaenet_getfunc *getfunc;
};

int uaenet_getdatalenght (void)
{
	return sizeof (struct uaenetdatawin32);
}

static void uaeser_initdata (struct uaenetdatawin32 *sd, void *user)
{
	memset (sd, 0, sizeof (struct uaenetdatawin32));
	sd->evttw = 0;
	sd->user = user;
	sd->fp = NULL;
}

#if 0
static void *uaenet_trap_thread (void *arg)
{
	struct uaenetdatawin32 *sd = arg;
	HANDLE handles[4];
	int cnt, towrite;
	int readactive, writeactive;
	DWORD actual;

	uae_set_thread_priority (NULL, 2);
	sd->threadactive = 1;
	uae_sem_post (&sd->sync_sem);
	readactive = 0;
	writeactive = 0;
	while (sd->threadactive == 1) {
		int donotwait = 0;

		uae_sem_wait (&sd->change_sem);

		if (readactive) {
			if (GetOverlappedResult (sd->hCom, &sd->olr, &actual, FALSE)) {
				readactive = 0;
				uaenet_gotdata (sd->user, sd->readbuffer, actual);
				donotwait = 1;
			}
		}
		if (writeactive) {
			if (GetOverlappedResult (sd->hCom, &sd->olw, &actual, FALSE)) {
				writeactive = 0;
				donotwait = 1;
			}
		}

		if (!readactive) {
			if (!ReadFile (sd->hCom, sd->readbuffer, sd->mtu, &actual, &sd->olr)) {
				DWORD err = GetLastError();
				if (err == ERROR_IO_PENDING)
					readactive = 1;
			} else {
				uaenet_gotdata (sd->user, sd->readbuffer, actual);
				donotwait = 1;
			}
		}

		towrite = 0;
		if (!writeactive && uaenet_getdata (sd->user, sd->writebuffer, &towrite)) {
			donotwait = 1;
			if (!WriteFile (sd->hCom, sd->writebuffer, towrite, &actual, &sd->olw)) {
				DWORD err = GetLastError();
				if (err == ERROR_IO_PENDING)
					writeactive = 1;
			}
		}

		uae_sem_post (&sd->change_sem);

		if (!donotwait) {
			cnt = 0;
			handles[cnt++] = sd->evtt;
			if (readactive)
				handles[cnt++] = sd->olr.hEvent;
			if (writeactive)
				handles[cnt++] = sd->olw.hEvent;
			WaitForMultipleObjects(cnt, handles, FALSE, INFINITE);
		}


	}
	sd->threadactive = 0;
	uae_sem_post (&sd->sync_sem);
	return 0;
}
#endif

static void *uaenet_trap_threadr (void *arg)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)arg;
	struct pcap_pkthdr *header;
	const u_char *pkt_data;

	uae_set_thread_priority (NULL, 1);
	sd->threadactiver = 1;
	uae_sem_post (&sd->sync_semr);
	while (sd->threadactiver == 1) {
		int r;
		r = pcap_next_ex (sd->fp, &header, &pkt_data);
		if (r == 1) {
			uae_sem_wait (&sd->change_sem);
			sd->gotfunc ((struct s2devstruct*)sd->user, pkt_data, header->len);
			uae_sem_post (&sd->change_sem);
		}
		if (r < 0) {
			write_log (_T("pcap_next_ex failed, err=%d\n"), r);
			break;
		}
	}
	sd->threadactiver = 0;
	uae_sem_post (&sd->sync_semr);
	return 0;
}

static void *uaenet_trap_threadw (void *arg)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)arg;

	uae_set_thread_priority (NULL, 1);
	sd->threadactivew = 1;
	uae_sem_post (&sd->sync_semw);
	while (sd->threadactivew == 1) {
		int donotwait = 0;
		int towrite = sd->mtu;
		uae_sem_wait (&sd->change_sem);
		if (sd->getfunc ((struct s2devstruct*)sd->user, sd->writebuffer, &towrite)) {
			pcap_sendpacket (sd->fp, sd->writebuffer, towrite);
			donotwait = 1;
		}
		uae_sem_post (&sd->change_sem);
		if (!donotwait)
			WaitForSingleObject (sd->evttw, INFINITE);
	}
	sd->threadactivew = 0;
	uae_sem_post (&sd->sync_semw);
	return 0;
}

void uaenet_trigger (void *vsd)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	if (!sd)
		return;
	SetEvent (sd->evttw);
}

int uaenet_open (void *vsd, struct netdriverdata *tc, void *user, uaenet_gotfunc *gotfunc, uaenet_getfunc *getfunc, int promiscuous)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	char *s;

	s = ua (tc->name);
	sd->fp = pcap_open (s, 65536, (promiscuous ? PCAP_OPENFLAG_PROMISCUOUS : 0) | PCAP_OPENFLAG_MAX_RESPONSIVENESS, 100, NULL, sd->errbuf);
	xfree (s);
	if (sd->fp == NULL) {
		TCHAR *ss = au (sd->errbuf);
		write_log (_T("'%s' failed to open: %s\n"), tc->name, ss);
		xfree (ss);
		return 0;
	}
	sd->tc = tc;
	sd->user = user;
	sd->evttw = CreateEvent (NULL, FALSE, FALSE, NULL);

	if (!sd->evttw)
		goto end;
	sd->mtu = tc->mtu;
	sd->readbuffer = xmalloc (uae_u8, sd->mtu);
	sd->writebuffer = xmalloc (uae_u8, sd->mtu);
	sd->gotfunc = gotfunc;
	sd->getfunc = getfunc;

	uae_sem_init (&sd->change_sem, 0, 1);
	uae_sem_init (&sd->sync_semr, 0, 0);
	uae_start_thread (_T("uaenet_win32r"), uaenet_trap_threadr, sd, &sd->tidr);
	uae_sem_wait (&sd->sync_semr);
	uae_sem_init (&sd->sync_semw, 0, 0);
	uae_start_thread (_T("uaenet_win32w"), uaenet_trap_threadw, sd, &sd->tidw);
	uae_sem_wait (&sd->sync_semw);
	write_log (_T("uaenet_win32 initialized\n"));
	return 1;

end:
	uaenet_close (sd);
	return 0;
}

void uaenet_close (void *vsd)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	if (!sd)
		return;
	if (sd->threadactiver) {
		sd->threadactiver = -1;
	}
	if (sd->threadactivew) {
		sd->threadactivew = -1;
		SetEvent (sd->evttw);
	}
	if (sd->threadactiver) {
		while (sd->threadactiver)
			Sleep(10);
		write_log (_T("uaenet_win32 thread %d killed\n"), sd->tidr);
		uae_end_thread (&sd->tidr);
	}
	if (sd->threadactivew) {
		while (sd->threadactivew)
			Sleep(10);
		CloseHandle (sd->evttw);
		write_log (_T("uaenet_win32 thread %d killed\n"), sd->tidw);
		uae_end_thread (&sd->tidw);
	}
	xfree (sd->readbuffer);
	xfree (sd->writebuffer);
	if (sd->fp)
		pcap_close (sd->fp);
	uaeser_initdata (sd, sd->user);
	write_log (_T("uaenet_win32 closed\n"));
}

void uaenet_enumerate_free (void)
{
	int i;

	for (i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
		tds[i].active = 0;
	}
	enumerated = 0;
}

static struct netdriverdata *enumit (const TCHAR *name)
{
	int cnt;
	
	if (name == NULL)
		return tds;
	for (cnt = 0; cnt < MAX_TOTAL_NET_DEVICES; cnt++) {
		TCHAR mac[20];
		struct netdriverdata *tc = tds + cnt;
		_stprintf (mac, _T("%02X:%02X:%02X:%02X:%02X:%02X"),
			tc->mac[0], tc->mac[1], tc->mac[2], tc->mac[3], tc->mac[4], tc->mac[5]);
		if (tc->active && name && (!_tcsicmp (name, tc->name) || !_tcsicmp (name, mac)))
			return tc;
	}
	return NULL;
}

struct netdriverdata *uaenet_enumerate (const TCHAR *name)
{
	static int done;
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_if_t *alldevs, *d;
	int cnt;
	HMODULE hm;
	LPADAPTER lpAdapter = 0;
	PPACKET_OID_DATA OidData;
	struct netdriverdata *tc, *tcp;
	pcap_t *fp;
	int val;
	TCHAR *ss;

	if (enumerated) {
		return enumit (name);
	}
	tcp = tds;
	hm = LoadLibrary (_T("wpcap.dll"));
	if (hm == NULL) {
		write_log (_T("uaenet: winpcap not installed (wpcap.dll)\n"));
		return NULL;
	}
	FreeLibrary (hm);
	hm = LoadLibrary (_T("packet.dll"));
	if (hm == NULL) {
		write_log (_T("uaenet: winpcap not installed (packet.dll)\n"));
		return NULL;
	}
	FreeLibrary (hm);
	if (!isdllversion (_T("wpcap.dll"), 4, 0, 0, 0)) {
		write_log (_T("uaenet: too old winpcap, v4 or newer required\n"));
		return NULL;
	}

	ss = au (pcap_lib_version ());
	if (!done)
		write_log (_T("uaenet: %s\n"), ss);
	xfree (ss);

	if (pcap_findalldevs_ex (PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) == -1) {
		ss = au (errbuf);
		write_log (_T("uaenet: failed to get interfaces: %s\n"), ss);
		xfree (ss);
		return NULL;
	}

	if (!done)
		write_log (_T("uaenet: detecting interfaces\n"));
	for(cnt = 0, d = alldevs; d != NULL; d = d->next) {
		char *n2;
		TCHAR *ss2;
		tc = tcp + cnt;
		if (cnt >= MAX_TOTAL_NET_DEVICES) {
			write_log (_T("buffer overflow\n"));
			break;
		}
		ss = au (d->name);
		ss2 = d->description ? au (d->description) : _T("(no description)");
		write_log (_T("%s\n- %s\n"), ss, ss2);
		xfree (ss2);
		xfree (ss);
		n2 = d->name;
		if (strlen (n2) <= strlen (PCAP_SRC_IF_STRING)) {
			write_log (_T("- corrupt name\n"));
			continue;
		}
		fp = pcap_open (d->name, 65536, 0, 0, NULL, errbuf);
		if (!fp) {
			ss = au (errbuf);
			write_log (_T("- pcap_open() failed: %s\n"), ss);
			xfree (ss);
			continue;
		}
		val = pcap_datalink (fp);
		pcap_close (fp);
		if (val != DLT_EN10MB) {
			if (!done)
				write_log (_T("- not an ethernet adapter (%d)\n"), val);
			continue;
		}

		lpAdapter = PacketOpenAdapter (n2 + strlen (PCAP_SRC_IF_STRING));
		if (lpAdapter == NULL) {
			if (!done)
				write_log (_T("- PacketOpenAdapter() failed\n"));
			continue;
		}
		OidData = (PPACKET_OID_DATA)xcalloc (uae_u8, 6 + sizeof(PACKET_OID_DATA));
		if (OidData) {
			OidData->Length = 6;
			OidData->Oid = OID_802_3_CURRENT_ADDRESS;
			if (PacketRequest (lpAdapter, FALSE, OidData)) {
				memcpy (tc->mac, OidData->Data, 6);
				if (!done)
					write_log (_T("- MAC %02X:%02X:%02X:%02X:%02X:%02X (%d)\n"),
					tc->mac[0], tc->mac[1], tc->mac[2],
					tc->mac[3], tc->mac[4], tc->mac[5], cnt);
				tc->type = UAENET_PCAP;
				tc->active = 1;
				tc->mtu = 1522;
				tc->name = au (d->name);
				tc->desc = au (d->description);
				cnt++;
			} else {
				write_log (_T(" - failed to get MAC\n"));
			}
			xfree (OidData);
		}
		PacketCloseAdapter (lpAdapter);
	}
	if (!done)
		write_log (_T("uaenet: end of detection, %d devices found.\n"), cnt);
	done = 1;
	pcap_freealldevs (alldevs);
	enumerated = 1;
	return enumit (name);
}

void uaenet_close_driver (struct netdriverdata *tc)
{
	int i;

	if (!tc)
		return;
	for (i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
		tc[i].active = 0;
	}
}
