/*
* UAE - The Un*x Amiga Emulator
*
* Win32 uaenet emulation
*
* Copyright 2007 Toni Wilen
*/

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <Iphlpapi.h>

#include "sysconfig.h"
#include "sysdeps.h"

#include <stdio.h>

#define HAVE_REMOTE
#define WPCAP
#define PCAP_DONT_INCLUDE_PCAP_BPF_H
#include "pcap.h"
#include "packet32.h"
#include "pcap/dlt.h"

#include "ntddndis.h"

#include "options.h"
#include "traps.h"
#include "sana2.h"

#include "threaddep/thread.h"
#include "win32_uaenet.h"
#include "win32.h"

int log_ethernet;
static struct netdriverdata tds[MAX_TOTAL_NET_DEVICES];
static int enumerated;
static int ethernet_paused;

typedef int(_cdecl *PCAP_FINDALLDEVS_EX)(char *source, struct pcap_rmtauth *auth, pcap_if_t **alldevs, char *errbuf);
static PCAP_FINDALLDEVS_EX ppcap_findalldevs_ex;
typedef void(_cdecl *PCAP_FREEALLDEVS)(pcap_if_t *);
static PCAP_FREEALLDEVS ppcap_freealldevs;
typedef pcap_t *(_cdecl *PCAP_OPEN)(const char *source, int snaplen, int flags, int read_timeout, struct pcap_rmtauth *auth, char *errbuf);
static PCAP_OPEN ppcap_open;
typedef void (_cdecl *PCAP_CLOSE)(pcap_t *);
static PCAP_CLOSE ppcap_close;
typedef int (_cdecl *PCAP_DATALINK)(pcap_t *);
static PCAP_DATALINK ppcap_datalink;
typedef int (_cdecl *PCAP_SENDPACKET)(pcap_t *, const u_char *, int);
static PCAP_SENDPACKET ppcap_sendpacket;
typedef int(_cdecl *PCAP_NEXT_EX)(pcap_t *, struct pcap_pkthdr **, const u_char **);
static PCAP_NEXT_EX ppcap_next_ex;
typedef const char *(_cdecl *PCAP_LIB_VERSION)(void);
static PCAP_LIB_VERSION ppcap_lib_version;

typedef LPADAPTER(_cdecl *PACKETOPENADAPTER)(PCHAR AdapterName);
static PACKETOPENADAPTER pPacketOpenAdapter;
typedef VOID(_cdecl *PACKETCLOSEADAPTER)(LPADAPTER lpAdapter);
static PACKETCLOSEADAPTER pPacketCloseAdapter;
typedef BOOLEAN (_cdecl *PACKETREQUEST)(LPADAPTER  AdapterObject, BOOLEAN Set, PPACKET_OID_DATA  OidData);
static PACKETREQUEST pPacketRequest;

static HMODULE wpcap, packet;

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
		r = ppcap_next_ex(sd->fp, &header, &pkt_data);
		if (r == 1 && !ethernet_paused) {
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
			if (log_ethernet & 1) {
				TCHAR out[1600 * 2], *p;
				p = out;
				for (int i = 0; i < towrite && i < 1600; i++) {
					_stprintf(p, _T("%02x"), sd->writebuffer[i]);
					p += 2;
					*p = 0;
				}
				write_log(_T("OUT %4d: %s\n"), towrite, out);
			}
			ppcap_sendpacket(sd->fp, sd->writebuffer, towrite);
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

// locally administered unicast MAC: U<<1, A<<1, E<<1
static const uae_u8 uaemac[] = { 0xaa, 0x82, 0x8a, 0x00, 0x00, 0x00 };

int uaenet_open (void *vsd, struct netdriverdata *tc, void *user, uaenet_gotfunc *gotfunc, uaenet_getfunc *getfunc, int promiscuous, const uae_u8 *mac)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	char *s;

	s = ua (tc->name);
	if (mac)
		memcpy(tc->mac, mac, 6);
	if (memcmp(tc->mac, tc->originalmac, 6)) {
		promiscuous = 1;
	}
	sd->fp = ppcap_open(s, 65536, (promiscuous ? PCAP_OPENFLAG_PROMISCUOUS : 0) | PCAP_OPENFLAG_MAX_RESPONSIVENESS, 100, NULL, sd->errbuf);
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
		ppcap_close (sd->fp);
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
	LPADAPTER lpAdapter = 0;
	PPACKET_OID_DATA OidData;
	struct netdriverdata *tc, *tcp;
	pcap_t *fp;
	int val;
	TCHAR *ss;
	bool npcap = true;
	TCHAR sname[MAX_DPATH];
	int isdll;

	if (enumerated) {
		return enumit (name);
	}
	tcp = tds;

	int len = GetSystemDirectory(sname, MAX_DPATH);
	if (len) {
		_tcscat(sname, _T("\\Npcap"));
		SetDllDirectory(sname);
	}
	wpcap = LoadLibrary(_T("wpcap.dll"));
	packet = LoadLibrary(_T("packet.dll"));
	isdll = isdllversion(_T("wpcap.dll"), 4, 0, 0, 0);
	SetDllDirectory(_T(""));
	if (wpcap == NULL) {
		FreeLibrary(packet);
		int err = GetLastError();
		wpcap = LoadLibrary (_T("wpcap.dll"));
		packet = LoadLibrary(_T("packet.dll"));
		isdll = isdllversion(_T("wpcap.dll"), 4, 0, 0, 0);
		if (wpcap == NULL) {
			write_log (_T("uaenet: npcap/winpcap not installed (wpcap.dll)\n"));
			return NULL;
		}
		npcap = false;
	}
	if (packet == NULL) {
		write_log (_T("uaenet: npcap/winpcap not installed (packet.dll)\n"));
		FreeLibrary(wpcap);
		wpcap = NULL;
		return NULL;
	}

	if (!isdll) {
		write_log (_T("uaenet: too old npcap/winpcap, v4 or newer required\n"));
		return NULL;
	}

	ppcap_lib_version = (PCAP_LIB_VERSION)GetProcAddress(wpcap, "pcap_lib_version");
	ppcap_findalldevs_ex = (PCAP_FINDALLDEVS_EX)GetProcAddress(wpcap, "pcap_findalldevs_ex");
	ppcap_freealldevs = (PCAP_FREEALLDEVS)GetProcAddress(wpcap, "pcap_freealldevs");
	ppcap_open = (PCAP_OPEN)GetProcAddress(wpcap, "pcap_open");
	ppcap_close = (PCAP_CLOSE)GetProcAddress(wpcap, "pcap_close");
	ppcap_datalink = (PCAP_DATALINK)GetProcAddress(wpcap, "pcap_datalink");

	ppcap_sendpacket = (PCAP_SENDPACKET)GetProcAddress(wpcap, "pcap_sendpacket");
	ppcap_next_ex = (PCAP_NEXT_EX)GetProcAddress(wpcap, "pcap_next_ex");

	pPacketOpenAdapter = (PACKETOPENADAPTER)GetProcAddress(packet, "PacketOpenAdapter");
	pPacketCloseAdapter = (PACKETCLOSEADAPTER)GetProcAddress(packet, "PacketCloseAdapter");
	pPacketRequest = (PACKETREQUEST)GetProcAddress(packet, "PacketRequest");

	ss = au (ppcap_lib_version());
	if (!done)
		write_log (_T("uaenet: %s\n"), ss);
	xfree (ss);

	if (ppcap_findalldevs_ex(PCAP_SRC_IF_STRING, NULL, &alldevs, errbuf) == -1) {
		ss = au (errbuf);
		write_log (_T("uaenet: failed to get interfaces: %s\n"), ss);
		xfree (ss);
		return NULL;
	}


	PIP_ADAPTER_ADDRESSES aa = NULL;
	DWORD aasize = 0;
	DWORD err = GetAdaptersAddresses(AF_UNSPEC,
		GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
		NULL, NULL, &aasize);
	if (err == ERROR_BUFFER_OVERFLOW) {
		aa = (IP_ADAPTER_ADDRESSES*)xcalloc(uae_u8, aasize);
		if (GetAdaptersAddresses(AF_UNSPEC,
			GAA_FLAG_SKIP_UNICAST | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
			NULL, aa, &aasize) != ERROR_SUCCESS)  {
			xfree(aa);
			aa = NULL;
		}
	}

	OidData = (PPACKET_OID_DATA)xcalloc(uae_u8, 6 + sizeof(PACKET_OID_DATA));
	OidData->Length = 6;
	OidData->Oid = OID_802_3_CURRENT_ADDRESS;

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
		fp = ppcap_open(d->name, 65536, 0, 0, NULL, errbuf);
		if (!fp) {
			ss = au (errbuf);
			write_log (_T("- pcap_open() failed: %s\n"), ss);
			xfree (ss);
			continue;
		}
		val = ppcap_datalink(fp);
		ppcap_close (fp);
		if (val != DLT_EN10MB) {
			if (!done)
				write_log (_T("- not an ethernet adapter (%d)\n"), val);
			continue;
		}

		lpAdapter = pPacketOpenAdapter(n2 + strlen (PCAP_SRC_IF_STRING));
		if (lpAdapter == NULL) {
			if (!done)
				write_log (_T("- PacketOpenAdapter() failed\n"));
			continue;
		}
		memset(tc->originalmac, 0, sizeof tc->originalmac);
		memcpy(tc->mac, uaemac, 6);
		if (pPacketRequest (lpAdapter, FALSE, OidData)) {
			memcpy(tc->mac, OidData->Data, 6);
			memcpy(tc->originalmac, tc->mac, 6);
		} else {
			PIP_ADAPTER_ADDRESSES aap = aa;
			while (aap) {
				char *name1 = aap->AdapterName;
				char *name2 = d->name;
				while (*name2 && *name2 != '{')
					name2++;
				if (*name2 && !stricmp(name1, name2)) {
					memcpy(tc->mac, aap->PhysicalAddress, 6);
					memcpy(tc->originalmac, tc->mac, 6);
					break;
				}
				aap = aap->Next;
			}
		}
		if (!done)
			write_log(_T("- MAC %02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X\n"),
				tc->mac[0], tc->mac[1], tc->mac[2], tc->mac[3], tc->mac[4], tc->mac[5],
				uaemac[0], uaemac[1], uaemac[2], tc->mac[3], tc->mac[4], tc->mac[5]);
		memcpy(tc->mac, uaemac, 3);
		tc->type = UAENET_PCAP;
		tc->active = 1;
		tc->mtu = 1522;
		tc->name = au(d->name);
		tc->desc = au(d->description);
		cnt++;
		pPacketCloseAdapter(lpAdapter);
	}
	if (!done)
		write_log (_T("uaenet: end of detection, %d devices found.\n"), cnt);
	done = 1;
	ppcap_freealldevs(alldevs);
	xfree(OidData);
	xfree(aa);
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

void ethernet_pause(int pause)
{
	ethernet_paused = pause;
}

void ethernet_reset(void)
{
	ethernet_paused = 0;
}
