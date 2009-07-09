/*
 * UAE - The Un*x Amiga Emulator
 *
 * Win32 uaenet emulation
 *
 * Copyright 2007 Toni Wilen
 */

#include "sysconfig.h"

#include <stdio.h>

#define HAVE_REMOTE
#define WPCAP
#include "pcap.h"

#include <windows.h>

#include "packet32.h"
#include "ntddndis.h"

#include "sysdeps.h"
#include "options.h"

#include "threaddep/thread.h"
#include "win32_uaenet.h"
#include "win32.h"


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
    struct uaenetdatawin32 *sd = arg;
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
	    uaenet_gotdata (sd->user, pkt_data, header->len);
	    uae_sem_post (&sd->change_sem);
	}
	if (r < 0) {
	    write_log (L"pcap_next_ex failed, err=%d\n", r);
	    break;
	}
    }
    sd->threadactiver = 0;
    uae_sem_post (&sd->sync_semr);
    return 0;
}

static void *uaenet_trap_threadw (void *arg)
{
    struct uaenetdatawin32 *sd = arg;

    uae_set_thread_priority (NULL, 1);
    sd->threadactivew = 1;
    uae_sem_post (&sd->sync_semw);
    while (sd->threadactivew == 1) {
	int donotwait = 0;
        int towrite;
	uae_sem_wait (&sd->change_sem);
	if (uaenet_getdata (sd->user, sd->writebuffer, &towrite)) {
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

void uaenet_trigger (struct uaenetdatawin32 *sd)
{
    if (!sd)
	return;
    SetEvent (sd->evttw);
}

int uaenet_open (struct uaenetdatawin32 *sd, struct netdriverdata *tc, void *user, int promiscuous)
{
    char *s;

    s = ua (tc->name);
    sd->fp = pcap_open (s, 65536, (promiscuous ? PCAP_OPENFLAG_PROMISCUOUS : 0) | PCAP_OPENFLAG_MAX_RESPONSIVENESS, 100, NULL, sd->errbuf);
    xfree (s);
    if (sd->fp == NULL) {
	TCHAR *ss = au (sd->errbuf);
	write_log (L"'%s' failed to open: %s\n", tc->name, ss);
	xfree (ss);
	return 0;
    }
    sd->tc = tc;
    sd->user = user;
    sd->evttw = CreateEvent (NULL, FALSE, FALSE, NULL);

    if (!sd->evttw)
	goto end;
    sd->mtu = tc->mtu;
    sd->readbuffer = xmalloc (sd->mtu);
    sd->writebuffer = xmalloc (sd->mtu);

    uae_sem_init (&sd->change_sem, 0, 1);
    uae_sem_init (&sd->sync_semr, 0, 0);
    uae_start_thread (L"uaenet_win32r", uaenet_trap_threadr, sd, &sd->tidr);
    uae_sem_wait (&sd->sync_semr);
    uae_sem_init (&sd->sync_semw, 0, 0);
    uae_start_thread (L"uaenet_win32w", uaenet_trap_threadw, sd, &sd->tidw);
    uae_sem_wait (&sd->sync_semw);
    write_log (L"uaenet_win32 initialized\n");
    return 1;

end:
    uaenet_close (sd);
    return 0;
}

void uaenet_close (struct uaenetdatawin32 *sd)
{
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
	write_log (L"uaenet_win32 thread %d killed\n", sd->tidr);
	uae_end_thread (&sd->tidr);
    }
    if (sd->threadactivew) {
	while (sd->threadactivew)
	    Sleep(10);
	CloseHandle (sd->evttw);
	write_log (L"uaenet_win32 thread %d killed\n", sd->tidw);
	uae_end_thread (&sd->tidw);
    }
    xfree (sd->readbuffer);
    xfree (sd->writebuffer);
    if (sd->fp)
	pcap_close (sd->fp);
    uaeser_initdata (sd, sd->user);
    write_log (L"uaenet_win32 closed\n");
}


int uaenet_open_driver (struct netdriverdata *tcp)
{
    char errbuf[PCAP_ERRBUF_SIZE];
    pcap_if_t *alldevs, *d;
    int cnt;
    HMODULE hm;
    LPADAPTER lpAdapter = 0;
    PPACKET_OID_DATA OidData;
    struct netdriverdata *tc;
    pcap_t *fp;
    int val;
    TCHAR *ss;

    hm = LoadLibrary (L"wpcap.dll");
    if (hm == NULL) {
	write_log (L"uaenet: winpcap not installed (wpcap.dll)\n");
	return 0;
    }
    FreeLibrary (hm);
    hm = LoadLibrary (L"packet.dll");
    if (hm == NULL) {
	write_log (L"uaenet: winpcap not installed (packet.dll)\n");
	return 0;
    }
    FreeLibrary (hm);
    ss = au (pcap_lib_version ());
    write_log (L"uaenet: %s\n", ss);
    xfree (ss);

    if (pcap_findalldevs_ex (PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) == -1) {
	ss = au (errbuf);
	write_log (L"uaenet: failed to get interfaces: %s\n", ss);
	xfree (ss);
	return 0;
    }

    write_log (L"uaenet: detecting interfaces\n");
    for(cnt = 0, d = alldevs; d != NULL; d = d->next) {
	char *n2;
	TCHAR *ss2;
	tc = tcp + cnt;
	if (cnt >= MAX_TOTAL_NET_DEVICES) {
	    write_log (L"buffer overflow\n");
	    break;
	}
	ss = au (d->name);
	ss2 = d->description ? au (d->description) : L"(no description)";
	write_log (L"%s\n- %s\n", ss, ss2);
	xfree (ss2);
	xfree (ss);
	n2 = d->name;
	if (strlen (n2) <= strlen (PCAP_SRC_IF_STRING)) {
	    write_log (L"- corrupt name\n");
	    continue;
	}
	fp = pcap_open (d->name, 65536, 0, 0, NULL, errbuf);
	if (!fp) {
	    ss = au (errbuf);
	    write_log (L"- pcap_open() failed: %s\n", ss);
	    xfree (ss);
	    continue;
	}
	val = pcap_datalink (fp);
	pcap_close (fp);
	if (val != DLT_EN10MB) {
	    write_log (L"- not an ethernet adapter (%d)\n", val);
	    continue;
	}

	lpAdapter = PacketOpenAdapter (n2 + strlen (PCAP_SRC_IF_STRING));
	if (lpAdapter == NULL) {
	    write_log (L"- PacketOpenAdapter() failed\n");
	    continue;
	}
	OidData = calloc(6 + sizeof(PACKET_OID_DATA), 1);
	if (OidData) {
	    OidData->Length = 6;
	    OidData->Oid = OID_802_3_CURRENT_ADDRESS;
	    if (PacketRequest (lpAdapter, FALSE, OidData)) {
		memcpy (tc->mac, OidData->Data, 6);
		write_log (L"- MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
		    tc->mac[0], tc->mac[1], tc->mac[2],
		    tc->mac[3], tc->mac[4], tc->mac[5]);
		write_log (L"- mapped as uaenet.device:%d\n", cnt++);
		tc->active = 1;
		tc->mtu = 1500;
		tc->name = au (d->name);
	    } else {
		write_log (L" - failed to get MAC\n");
	    }
	    free (OidData);
	}
        PacketCloseAdapter (lpAdapter);
    }
    write_log (L"uaenet: end of detection\n");
    pcap_freealldevs(alldevs);
    return 0;
}

void uaenet_close_driver (struct netdriverdata *tc)
{
    int i;

    for (i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
	tc[i].active = 0;
    }
}


