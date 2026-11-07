/*
* UAE - The Un*x Amiga Emulator
*
* Win32 uaenet emulation
*
* Copyright 2007 Toni Wilen
*/

#include <winsock2.h>
#include <iphlpapi.h>

#include "sysconfig.h"
#include "sysdeps.h"

#define HAVE_REMOTE
#define WPCAP
#define PCAP_DONT_INCLUDE_PCAP_BPF_H
#include "pcap.h"
#include "Packet32.h"
#include "pcap/dlt.h"

#include "ntddndis.h"

#include "options.h"
#include "sana2.h"

#include "threaddep/thread.h"
#include "uaenet_common.h"
#include "win32.h"

static struct netdriverdata tds[MAX_TOTAL_NET_DEVICES];
static int enumerated;

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
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *fp;
	uaenet_worker *worker;
};

int uaenet_getdatalenght(void)
{
	return sizeof(struct uaenetdatawin32);
}

static int receive_packet(void *user, uae_u8 *, int, const uae_u8 **packet, int *length)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)user;
	struct pcap_pkthdr *header = NULL;
	const u_char *data = NULL;
	int result = ppcap_next_ex(sd->fp, &header, &data);
	if (result == 1 && header && data) {
		*packet = data;
		*length = (int)(header->caplen < header->len ? header->caplen : header->len);
	} else if (result < 0) {
		write_log(_T("pcap_next_ex failed, err=%d\n"), result);
	}
	return result;
}

static void send_packet(void *user, const uae_u8 *data, int length)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)user;
	ppcap_sendpacket(sd->fp, data, length);
}

static const uaenet_backend packet_backend = { receive_packet, send_packet, NULL };

void uaenet_trigger(void *vsd)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	if (sd) {
		uaenet_wake_writer(sd->worker);
	}
}

int uaenet_open (void *vsd, struct netdriverdata *tc, void *user, uaenet_gotfunc *gotfunc, uaenet_getfunc *getfunc, int promiscuous, const uae_u8 *mac)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	char *s;

	memset(sd, 0, sizeof(*sd));
	s = ua(tc->name);
	promiscuous = uaenet_select_mac(tc, mac, promiscuous);
	sd->fp = ppcap_open(s, 65536, (promiscuous ? PCAP_OPENFLAG_PROMISCUOUS : 0) | PCAP_OPENFLAG_MAX_RESPONSIVENESS, 100, NULL, sd->errbuf);
	xfree (s);
	if (sd->fp == NULL) {
		TCHAR *ss = au (sd->errbuf);
		write_log (_T("'%s' failed to open: %s\n"), tc->name, ss);
		xfree (ss);
		return 0;
	}
	sd->worker = uaenet_start_workers(&packet_backend, sd, user, gotfunc, getfunc, tc->mtu, -1);
	if (!sd->worker) {
		uaenet_close(sd);
		return 0;
	}
	write_log (_T("uaenet_win32 initialized\n"));
	return 1;

}

void uaenet_close(void *vsd)
{
	struct uaenetdatawin32 *sd = (struct uaenetdatawin32*)vsd;
	if (!sd) {
		return;
	}
	uaenet_stop_workers(sd->worker);
	if (sd->fp) {
		ppcap_close(sd->fp);
	}
	memset(sd, 0, sizeof(*sd));
	write_log(_T("uaenet_win32 closed\n"));
}

void uaenet_enumerate_free(void)
{
	uaenet_clear_drivers(tds, MAX_TOTAL_NET_DEVICES, false);
	enumerated = 0;
}

static struct netdriverdata *enumit(const TCHAR *name)
{
	return uaenet_find_driver(tds, MAX_TOTAL_NET_DEVICES, name);
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
	SetDllDirectory(_T(""));
	if (wpcap == NULL) {
		FreeLibrary(packet);
		int err = GetLastError();
		wpcap = LoadLibrary (_T("wpcap.dll"));
		packet = LoadLibrary(_T("packet.dll"));
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

	if (!ppcap_lib_version || !ppcap_findalldevs_ex || !ppcap_freealldevs ||
		!ppcap_open || !ppcap_close || !ppcap_datalink || !ppcap_sendpacket ||
		!ppcap_next_ex || !pPacketOpenAdapter || !pPacketCloseAdapter || !pPacketRequest) {
		write_log(_T("uaenet: too old npcap/winpcap, missing function(s).\n"));
		return NULL;
	}

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
		ss2 = au (d->description ? d->description : "(no description)");
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
		memcpy(tc->mac, uaenet_mac, 6);
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
				uaenet_mac[0], uaenet_mac[1], uaenet_mac[2], tc->mac[3], tc->mac[4], tc->mac[5]);
		memcpy(tc->mac, uaenet_mac, 3);
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

void uaenet_close_driver(struct netdriverdata *tc)
{
	if (tc) {
		uaenet_clear_drivers(tds, MAX_TOTAL_NET_DEVICES, false);
	}
}
