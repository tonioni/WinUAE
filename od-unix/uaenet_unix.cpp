/*
 * UAE - The Un*x Amiga Emulator
 *
 * Unix uaenet packet backend
 *
 * Copyright 2026 WinUAE contributors
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <pcap/pcap.h>
#include <pcap/dlt.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#include <net/if_dl.h>
#include <net/if_types.h>
#endif
#if defined(__linux__)
#include <net/if_arp.h>
#include <linux/if_tun.h>
#include <netpacket/packet.h>
#endif

#include "options.h"
#include "sana2.h"
#include "threaddep/thread.h"
#include "uaenet.h"
#include "uae.h"

int log_ethernet;

static struct netdriverdata tds[MAX_TOTAL_NET_DEVICES];
static int enumerated;
static int enumerated_count;
static int ethernet_paused;

static const uae_u8 uaemac[] = { 0xaa, 0x82, 0x8a, 0x00, 0x00, 0x00 };

enum
{
	UAENET_BACKEND_NONE,
	UAENET_BACKEND_PCAP,
	UAENET_BACKEND_TAP,
	UAENET_BACKEND_TUN
};

struct uaenetdataunix
{
	void *user;
	struct netdriverdata *tc;
	uae_u8 *readbuffer;
	uae_u8 *writebuffer;
	int mtu;

	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t *fp;
	int backend;
	int fd;
	uaenet_gotfunc *gotfunc;
	uaenet_getfunc *getfunc;

	volatile int threadactiver;
	uae_thread_id tidr;
	uae_sem_t sync_semr;
	volatile int threadactivew;
	uae_thread_id tidw;
	uae_sem_t sync_semw;
	uae_sem_t change_sem;
	uae_sem_t write_sem;
};

int uaenet_getdatalenght(void)
{
	return sizeof(struct uaenetdataunix);
}

static void uaenet_initdata(struct uaenetdataunix *sd, void *user)
{
	memset(sd, 0, sizeof(*sd));
	sd->user = user;
	sd->fd = -1;
}

static uae_u16 get_be16(const uae_u8 *p)
{
	return ((uae_u16)p[0] << 8) | p[1];
}

static void put_be16(uae_u8 *p, uae_u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static bool name_has_prefix(const char *name, const char *prefix)
{
	return name && !strncmp(name, prefix, strlen(prefix));
}

static bool tname_has_prefix(const TCHAR *name, const TCHAR *prefix)
{
	return name && !_tcsncmp(name, prefix, _tcslen(prefix));
}

static const char *tuntap_interface_name(const char *name)
{
	const char *p = strchr(name, ':');
	return p ? p + 1 : name;
}

static bool uaenet_is_tuntap_name(const TCHAR *name)
{
	return tname_has_prefix(name, _T("tap:")) || tname_has_prefix(name, _T("tun:"));
}

static void make_fallback_macs(struct netdriverdata *tc, int index)
{
	static const uae_u8 hostmac[] = { 0xaa, 0x82, 0x8a, 0xfe, 0xfe, 0x00 };
	memcpy(tc->originalmac, hostmac, 6);
	memcpy(tc->mac, uaemac, 6);
	tc->originalmac[5] = (uae_u8)(index + 1);
	tc->mac[5] = (uae_u8)(index + 1);
}

static bool write_fd_packet(struct uaenetdataunix *sd, const uae_u8 *data, int len)
{
	const uae_u8 *src = data;
	int outlen = len;

	if (sd->backend == UAENET_BACKEND_TUN) {
		if (len < 14) {
			return true;
		}
		const uae_u16 ethertype = get_be16(data + 12);
		if (ethertype == 0x0800 || ethertype == 0x86dd) {
			src = data + 14;
			outlen = len - 14;
		} else {
			return true;
		}
	}

	while (outlen > 0) {
		const ssize_t done = write(sd->fd, src, outlen);
		if (done < 0) {
			if (errno == EINTR) {
				continue;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				return false;
			}
			write_log(_T("uaenet: TAP/TUN write failed: %s\n"), strerror(errno));
			return false;
		}
		if (done == 0) {
			return false;
		}
		src += done;
		outlen -= done;
	}
	return true;
}

static bool reply_tun_arp(struct uaenetdataunix *sd, const uae_u8 *data, int len)
{
	if (len < 42 || get_be16(data + 12) != 0x0806) {
		return false;
	}

	const uae_u8 *arp = data + 14;
	if (get_be16(arp + 0) != 1 || get_be16(arp + 2) != 0x0800 ||
		arp[4] != 6 || arp[5] != 4 || get_be16(arp + 6) != 1) {
		return false;
	}

	uae_u8 reply[42];
	memcpy(reply + 0, data + 6, 6);
	memcpy(reply + 6, sd->tc->originalmac, 6);
	put_be16(reply + 12, 0x0806);
	put_be16(reply + 14, 1);
	put_be16(reply + 16, 0x0800);
	reply[18] = 6;
	reply[19] = 4;
	put_be16(reply + 20, 2);
	memcpy(reply + 22, sd->tc->originalmac, 6);
	memcpy(reply + 28, arp + 24, 4);
	memcpy(reply + 32, arp + 8, 6);
	memcpy(reply + 38, arp + 14, 4);

	if (!ethernet_paused) {
		sd->gotfunc((struct s2devstruct*)sd->user, reply, sizeof(reply));
	}
	return true;
}

static bool send_packet(struct uaenetdataunix *sd, const uae_u8 *data, int len)
{
	if (sd->backend == UAENET_BACKEND_PCAP) {
		if (pcap_sendpacket(sd->fp, data, len) < 0) {
			TCHAR *err = au(pcap_geterr(sd->fp));
			write_log(_T("uaenet: pcap_sendpacket failed: %s\n"), err);
			xfree(err);
			return false;
		}
		return true;
	}

	if (sd->backend == UAENET_BACKEND_TUN && reply_tun_arp(sd, data, len)) {
		return true;
	}

	return write_fd_packet(sd, data, len);
}

static void uaenet_trap_threadr(void *arg)
{
	struct uaenetdataunix *sd = (struct uaenetdataunix*)arg;

	uae_set_thread_priority(NULL, 1);
	sd->threadactiver = 1;
	uae_sem_post(&sd->sync_semr);

	while (sd->threadactiver == 1) {
		if (sd->backend == UAENET_BACKEND_PCAP) {
			struct pcap_pkthdr *header = NULL;
			const u_char *pkt_data = NULL;
			const int r = pcap_next_ex(sd->fp, &header, &pkt_data);
			if (r == 1 && header && pkt_data && !ethernet_paused) {
				const int len = std::min<int>((int)header->caplen, (int)header->len);
				if (len > 0) {
					uae_sem_wait(&sd->change_sem);
					sd->gotfunc((struct s2devstruct*)sd->user, pkt_data, len);
					uae_sem_post(&sd->change_sem);
				}
			} else if (r == 0) {
				continue;
			} else if (r == PCAP_ERROR_BREAK && sd->threadactiver != 1) {
				break;
			} else if (r < 0) {
				write_log(_T("uaenet: pcap_next_ex failed, err=%d\n"), r);
				break;
			}
		} else {
			struct pollfd pfd;
			pfd.fd = sd->fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			const int pr = poll(&pfd, 1, 100);
			if (pr < 0) {
				if (errno == EINTR) {
					continue;
				}
				write_log(_T("uaenet: TAP/TUN poll failed: %s\n"), strerror(errno));
				break;
			}
			if (pr == 0 || !(pfd.revents & POLLIN)) {
				continue;
			}

			uae_u8 *pkt_data = sd->readbuffer;
			ssize_t got;
			if (sd->backend == UAENET_BACKEND_TUN) {
				got = read(sd->fd, sd->readbuffer + 14, sd->mtu);
				if (got > 0) {
					const uae_u8 version = sd->readbuffer[14] >> 4;
					if (version == 4 || version == 6) {
						memcpy(sd->readbuffer + 0, sd->tc->mac, 6);
						memcpy(sd->readbuffer + 6, sd->tc->originalmac, 6);
						put_be16(sd->readbuffer + 12, version == 4 ? 0x0800 : 0x86dd);
						got += 14;
					} else {
						got = 0;
					}
				}
			} else {
				got = read(sd->fd, sd->readbuffer, sd->mtu + 64);
			}
			if (got < 0) {
				if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
					continue;
				}
				write_log(_T("uaenet: TAP/TUN read failed: %s\n"), strerror(errno));
				break;
			}
			if (got > 0 && !ethernet_paused) {
				uae_sem_wait(&sd->change_sem);
				sd->gotfunc((struct s2devstruct*)sd->user, pkt_data, (int)got);
				uae_sem_post(&sd->change_sem);
			}
		}
	}

	sd->threadactiver = 0;
	uae_sem_post(&sd->sync_semr);
}

static void uaenet_trap_threadw(void *arg)
{
	struct uaenetdataunix *sd = (struct uaenetdataunix*)arg;

	uae_set_thread_priority(NULL, 1);
	sd->threadactivew = 1;
	uae_sem_post(&sd->sync_semw);

	while (sd->threadactivew == 1) {
		int towrite = sd->mtu;
		bool wrote = false;

		uae_sem_wait(&sd->change_sem);
		if (sd->getfunc((struct s2devstruct*)sd->user, sd->writebuffer, &towrite)) {
			if (log_ethernet & 1) {
				TCHAR out[1600 * 2], *p = out;
				for (int i = 0; i < towrite && i < 1600; i++) {
					_stprintf(p, _T("%02x"), sd->writebuffer[i]);
					p += 2;
					*p = 0;
				}
				write_log(_T("OUT %4d: %s\n"), towrite, out);
			}
			send_packet(sd, sd->writebuffer, towrite);
			wrote = true;
		}
		uae_sem_post(&sd->change_sem);

		if (!wrote) {
			uae_sem_trywait_delay(&sd->write_sem, 100);
		}
	}

	sd->threadactivew = 0;
	uae_sem_post(&sd->sync_semw);
}

void uaenet_trigger(void *vsd)
{
	struct uaenetdataunix *sd = (struct uaenetdataunix*)vsd;
	if (sd) {
		uae_sem_post(&sd->write_sem);
	}
}

static bool get_interface_mac_2(const char *name, uae_u8 *mac, bool ethernet_only)
{
	struct ifaddrs *ifaddr = NULL;
	bool found = false;

	if (getifaddrs(&ifaddr) != 0) {
		return false;
	}

	for (const struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_name || !ifa->ifa_addr || strcmp(ifa->ifa_name, name) != 0) {
			continue;
		}
#if defined(__linux__)
		if (ifa->ifa_addr->sa_family == AF_PACKET) {
			const struct sockaddr_ll *sll = (const struct sockaddr_ll*)ifa->ifa_addr;
			if (sll->sll_halen >= 6 && (!ethernet_only || sll->sll_hatype == ARPHRD_ETHER)) {
				memcpy(mac, sll->sll_addr, 6);
				found = true;
				break;
			}
		}
#elif defined(AF_LINK)
		if (ifa->ifa_addr->sa_family == AF_LINK) {
			const struct sockaddr_dl *sdl = (const struct sockaddr_dl*)ifa->ifa_addr;
			if (sdl->sdl_alen >= 6 && (!ethernet_only || sdl->sdl_type == IFT_ETHER)) {
				memcpy(mac, LLADDR(sdl), 6);
				found = true;
				break;
			}
		}
#endif
	}

	freeifaddrs(ifaddr);
	return found;
}

static bool get_interface_mac(const char *name, uae_u8 *mac)
{
	return get_interface_mac_2(name, mac, false);
}

static bool get_ethernet_interface_mac(const char *name, uae_u8 *mac)
{
	return get_interface_mac_2(name, mac, true);
}

static bool set_nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int open_tuntap_device(struct netdriverdata *tc, const char *name)
{
	const char *ifname = tuntap_interface_name(name);
	const bool istun = tc->type == UAENET_TUN;

#if defined(__linux__)
	int fd = open("/dev/net/tun", O_RDWR);
	if (fd < 0) {
		write_log(_T("uaenet: failed to open /dev/net/tun: %s\n"), strerror(errno));
		return -1;
	}

	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	ifr.ifr_flags = (istun ? IFF_TUN : IFF_TAP) | IFF_NO_PI;
	if (ifname && ifname[0] && strcmp(ifname, "auto")) {
		strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
	}
	if (ioctl(fd, TUNSETIFF, (void*)&ifr) < 0) {
		write_log(_T("uaenet: TUNSETIFF '%s' failed: %s\n"), ifname, strerror(errno));
		close(fd);
		return -1;
	}
	if (!set_nonblocking(fd)) {
		write_log(_T("uaenet: failed to set '%s' non-blocking: %s\n"), ifr.ifr_name, strerror(errno));
	}
	return fd;
#else
	char path[256];
	if (name_has_prefix(ifname, "/dev/")) {
		snprintf(path, sizeof(path), "%s", ifname);
	} else {
		snprintf(path, sizeof(path), "/dev/%s", ifname);
	}
	int fd = open(path, O_RDWR);
	if (fd < 0) {
		write_log(_T("uaenet: failed to open %s: %s\n"), path, strerror(errno));
		return -1;
	}
	if (!set_nonblocking(fd)) {
		write_log(_T("uaenet: failed to set '%s' non-blocking: %s\n"), path, strerror(errno));
	}
	return fd;
#endif
}

int uaenet_open(void *vsd, struct netdriverdata *tc, void *user, uaenet_gotfunc *gotfunc, uaenet_getfunc *getfunc, int promiscuous, const uae_u8 *mac)
{
	struct uaenetdataunix *sd = (struct uaenetdataunix*)vsd;
	char *name;

	uaenet_initdata(sd, user);
	name = ua(tc->name);
	if (mac) {
		memcpy(tc->mac, mac, 6);
	}
	if (memcmp(tc->mac, tc->originalmac, 6) != 0) {
		promiscuous = 1;
	}

	if (tc->type == UAENET_PCAP) {
		sd->backend = UAENET_BACKEND_PCAP;
		sd->fp = pcap_open_live(name, 65536, promiscuous ? 1 : 0, 100, sd->errbuf);
		xfree(name);
		if (!sd->fp) {
			TCHAR *err = au(sd->errbuf);
			write_log(_T("uaenet: '%s' failed to open: %s\n"), tc->name, err);
			xfree(err);
			return 0;
		}

		if (pcap_datalink(sd->fp) != DLT_EN10MB) {
			write_log(_T("uaenet: '%s' is not an Ethernet adapter\n"), tc->name);
			uaenet_close(sd);
			return 0;
		}
	} else if (tc->type == UAENET_TAP || tc->type == UAENET_TUN) {
		sd->backend = tc->type == UAENET_TAP ? UAENET_BACKEND_TAP : UAENET_BACKEND_TUN;
		sd->fd = open_tuntap_device(tc, name);
		xfree(name);
		if (sd->fd < 0) {
			return 0;
		}
	} else {
		xfree(name);
		return 0;
	}

	sd->tc = tc;
	sd->user = user;
	sd->mtu = tc->mtu > 0 ? tc->mtu : 1522;
	sd->readbuffer = xmalloc(uae_u8, sd->mtu + 64);
	sd->writebuffer = xmalloc(uae_u8, sd->mtu + 64);
	sd->gotfunc = gotfunc;
	sd->getfunc = getfunc;

	uae_sem_init(&sd->change_sem, 0, 1);
	uae_sem_init(&sd->write_sem, 0, 0);
	uae_sem_init(&sd->sync_semr, 0, 0);
	if (!uae_start_thread(_T("uaenet_unixr"), uaenet_trap_threadr, sd, &sd->tidr)) {
		goto end;
	}
	uae_sem_wait(&sd->sync_semr);

	uae_sem_init(&sd->sync_semw, 0, 0);
	if (!uae_start_thread(_T("uaenet_unixw"), uaenet_trap_threadw, sd, &sd->tidw)) {
		goto end;
	}
	uae_sem_wait(&sd->sync_semw);

	write_log(_T("uaenet_unix initialized (%s)\n"),
		sd->backend == UAENET_BACKEND_PCAP ? _T("pcap") : (sd->backend == UAENET_BACKEND_TAP ? _T("tap") : _T("tun")));
	return 1;

end:
	uaenet_close(sd);
	return 0;
}

void uaenet_close(void *vsd)
{
	struct uaenetdataunix *sd = (struct uaenetdataunix*)vsd;
	if (!sd) {
		return;
	}

	if (sd->threadactiver) {
		sd->threadactiver = -1;
		if (sd->fp) {
			pcap_breakloop(sd->fp);
		}
		uae_wait_thread(sd->tidr);
		write_log(_T("uaenet_unix read thread stopped\n"));
	}
	if (sd->threadactivew) {
		sd->threadactivew = -1;
		uae_sem_post(&sd->write_sem);
		uae_wait_thread(sd->tidw);
		write_log(_T("uaenet_unix write thread stopped\n"));
	}

	uae_sem_destroy(&sd->sync_semr);
	uae_sem_destroy(&sd->sync_semw);
	uae_sem_destroy(&sd->change_sem);
	uae_sem_destroy(&sd->write_sem);

	xfree(sd->readbuffer);
	xfree(sd->writebuffer);
	if (sd->fp) {
		pcap_close(sd->fp);
	}
	if (sd->fd >= 0) {
		close(sd->fd);
	}
	uaenet_initdata(sd, sd->user);
	write_log(_T("uaenet_unix closed\n"));
}

static bool has_enumerated_name(const TCHAR *name)
{
	for (int i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
		if (tds[i].active && tds[i].name && !_tcsicmp(tds[i].name, name)) {
			return true;
		}
	}
	return false;
}

static struct netdriverdata *add_pcap_entry(int *cntp, const char *ifname, const char *desc, bool verified)
{
	if (!ifname || !ifname[0] || *cntp >= MAX_TOTAL_NET_DEVICES) {
		return NULL;
	}

	TCHAR *tname = au(ifname);
	if (has_enumerated_name(tname)) {
		xfree(tname);
		return NULL;
	}

	char dbuf[512];
	if (verified) {
		snprintf(dbuf, sizeof(dbuf), "%s", desc ? desc : ifname);
	} else {
		snprintf(dbuf, sizeof(dbuf), "%s (pcap, permission needed)", desc ? desc : ifname);
	}

	TCHAR *tdesc = au(dbuf);
	struct netdriverdata *tc = &tds[*cntp];
	memset(tc, 0, sizeof(*tc));
	memcpy(tc->mac, uaemac, 6);
	if ((verified ? get_interface_mac : get_ethernet_interface_mac)(ifname, tc->mac)) {
		memcpy(tc->originalmac, tc->mac, 6);
	} else {
		if (!verified) {
			xfree(tname);
			xfree(tdesc);
			return NULL;
		}
		make_fallback_macs(tc, *cntp);
	}

	write_log(_T("- MAC %02X:%02X:%02X:%02X:%02X:%02X -> %02X:%02X:%02X:%02X:%02X:%02X\n"),
		tc->originalmac[0], tc->originalmac[1], tc->originalmac[2], tc->originalmac[3], tc->originalmac[4], tc->originalmac[5],
		uaemac[0], uaemac[1], uaemac[2], tc->originalmac[3], tc->originalmac[4], tc->originalmac[5]);
	memcpy(tc->mac, uaemac, 3);
	tc->mac[3] = tc->originalmac[3];
	tc->mac[4] = tc->originalmac[4];
	tc->mac[5] = tc->originalmac[5];
	tc->type = UAENET_PCAP;
	tc->active = 1;
	tc->mtu = 1522;
	tc->name = tname;
	tc->desc = tdesc;
	(*cntp)++;
	return tc;
}

static struct netdriverdata *add_tuntap_entry(int *cntp, int type, const char *ifname)
{
	if (!ifname || !ifname[0] || *cntp >= MAX_TOTAL_NET_DEVICES) {
		return NULL;
	}

	char nbuf[256];
	char dbuf[320];
	snprintf(nbuf, sizeof(nbuf), "%s:%s", type == UAENET_TAP ? "tap" : "tun", ifname);
	snprintf(dbuf, sizeof(dbuf), "%s interface %s (direct)", type == UAENET_TAP ? "TAP" : "TUN", ifname);

	TCHAR *tname = au(nbuf);
	if (has_enumerated_name(tname)) {
		xfree(tname);
		return NULL;
	}

	TCHAR *tdesc = au(dbuf);
	struct netdriverdata *tc = &tds[*cntp];
	memset(tc, 0, sizeof(*tc));
	make_fallback_macs(tc, *cntp);
	if (type == UAENET_TAP && get_interface_mac(ifname, tc->originalmac)) {
		memcpy(tc->mac, tc->originalmac, 6);
		memcpy(tc->mac, uaemac, 3);
	}
	tc->type = type;
	tc->active = 1;
	tc->mtu = 1500;
	tc->name = tname;
	tc->desc = tdesc;
	(*cntp)++;
	return tc;
}

static struct netdriverdata *add_tuntap_config_entry(const TCHAR *name)
{
	char *aname;
	struct netdriverdata *tc;

	if (!uaenet_is_tuntap_name(name) || enumerated_count >= MAX_TOTAL_NET_DEVICES) {
		return NULL;
	}

	aname = ua(name);
	const int type = name_has_prefix(aname, "tun:") ? UAENET_TUN : UAENET_TAP;
	const char *ifname = tuntap_interface_name(aname);
	int cnt = enumerated_count;
	tc = add_tuntap_entry(&cnt, type, ifname);
	enumerated_count = cnt;
	xfree(aname);
	return tc;
}

static int enumerate_tuntap_interfaces(int cnt)
{
#if defined(__linux__)
	struct ifaddrs *ifaddr = NULL;
	if (access("/dev/net/tun", F_OK) != 0) {
		return cnt;
	}
	if (getifaddrs(&ifaddr) != 0) {
		return cnt;
	}
	for (const struct ifaddrs *ifa = ifaddr; ifa && cnt < MAX_TOTAL_NET_DEVICES; ifa = ifa->ifa_next) {
		if (!ifa->ifa_name || !ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_PACKET) {
			continue;
		}
		int type = 0;
		if (name_has_prefix(ifa->ifa_name, "tap")) {
			type = UAENET_TAP;
		}
		if (name_has_prefix(ifa->ifa_name, "tun")) {
			type = UAENET_TUN;
		}
		if (type) {
			add_tuntap_entry(&cnt, type, ifa->ifa_name);
		}
	}
	freeifaddrs(ifaddr);
#else
	for (int i = 0; i < 32 && cnt < MAX_TOTAL_NET_DEVICES; i++) {
		char path[64];
		char ifname[32];
		snprintf(path, sizeof(path), "/dev/tap%d", i);
		if (access(path, F_OK) == 0) {
			snprintf(ifname, sizeof(ifname), "tap%d", i);
			add_tuntap_entry(&cnt, UAENET_TAP, ifname);
		}
		snprintf(path, sizeof(path), "/dev/tun%d", i);
		if (access(path, F_OK) == 0) {
			snprintf(ifname, sizeof(ifname), "tun%d", i);
			add_tuntap_entry(&cnt, UAENET_TUN, ifname);
		}
	}
#endif
	return cnt;
}

void uaenet_enumerate_free(void)
{
	for (int i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
		if (tds[i].name) {
			xfree((void*)tds[i].name);
		}
		if (tds[i].desc) {
			xfree((void*)tds[i].desc);
		}
		tds[i].active = 0;
		tds[i].name = NULL;
		tds[i].desc = NULL;
	}
	enumerated = 0;
	enumerated_count = 0;
}

static struct netdriverdata *enumit(const TCHAR *name)
{
	if (!name) {
		return tds;
	}
	for (int i = 0; i < enumerated_count; i++) {
		TCHAR mac[20];
		struct netdriverdata *tc = &tds[i];
		_stprintf(mac, _T("%02X:%02X:%02X:%02X:%02X:%02X"),
			tc->mac[0], tc->mac[1], tc->mac[2], tc->mac[3], tc->mac[4], tc->mac[5]);
		if (tc->active && (!_tcsicmp(name, tc->name) || !_tcsicmp(name, mac))) {
			return tc;
		}
	}
	if (uaenet_is_tuntap_name(name)) {
		return add_tuntap_config_entry(name);
	}
	return NULL;
}

struct netdriverdata *uaenet_enumerate(const TCHAR *name)
{
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_if_t *alldevs = NULL;
	int cnt = 0;

	if (enumerated) {
		return enumit(name);
	}

	if (pcap_findalldevs(&alldevs, errbuf) < 0) {
		TCHAR *err = au(errbuf);
		write_log(_T("uaenet: failed to get interfaces: %s\n"), err);
		xfree(err);
	} else {
		if (pcap_lib_version()) {
			TCHAR *version = au(pcap_lib_version());
			write_log(_T("uaenet: %s\n"), version);
			xfree(version);
		} else {
			write_log(_T("uaenet: libpcap\n"));
		}
		write_log(_T("uaenet: detecting interfaces\n"));

		for (pcap_if_t *d = alldevs; d && cnt < MAX_TOTAL_NET_DEVICES; d = d->next) {
			pcap_t *fp;
			char openerr[PCAP_ERRBUF_SIZE];
			const char *desc = d->description ? d->description : d->name;
			TCHAR *tname = au(d->name);
			TCHAR *tdesc = au(desc);

			write_log(_T("%s\n- %s\n"), tname, tdesc);

			fp = pcap_open_live(d->name, 65536, 0, 0, openerr);
			if (!fp) {
				TCHAR *err = au(openerr);
				write_log(_T("- pcap_open_live() failed: %s\n"), err);
				xfree(err);
				xfree(tname);
				xfree(tdesc);
				add_pcap_entry(&cnt, d->name, desc, false);
				continue;
			}
			const int datalink = pcap_datalink(fp);
			pcap_close(fp);
			if (datalink != DLT_EN10MB) {
				write_log(_T("- not an Ethernet adapter (%d)\n"), datalink);
				xfree(tname);
				xfree(tdesc);
				continue;
			}

			if (add_pcap_entry(&cnt, d->name, desc, true)) {
				tname = NULL;
				tdesc = NULL;
			}
			xfree(tname);
			xfree(tdesc);
		}
		pcap_freealldevs(alldevs);
	}

	cnt = enumerate_tuntap_interfaces(cnt);
	enumerated_count = cnt;
	write_log(_T("uaenet: end of detection, %d devices found.\n"), cnt);
	enumerated = 1;
	return enumit(name);
}

void uaenet_close_driver(struct netdriverdata *tc)
{
	if (!tc) {
		return;
	}
	for (int i = 0; i < MAX_TOTAL_NET_DEVICES; i++) {
		tds[i].active = 0;
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
