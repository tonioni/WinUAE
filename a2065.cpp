/*
* UAE - The Un*x Amiga Emulator
*
* A2065 ZorroII Ethernet Card
*
* Copyright 2009 Toni Wilen
*
*/

#include "sysconfig.h"
#include "sysdeps.h"

#ifdef A2065

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "a2065.h"
#include "ethernet.h"
#include "crc32.h"
#include "savestate.h"
#include "autoconf.h"

#define DUMPPACKET 0

#define MEM_MIN 0x8100
int log_a2065 = 0;
static int log_transmit = 1;
static int log_receive = 1;
int a2065_promiscuous = 0;

#define RAP 0x4002
#define RDP 0x4000
#define CHIP_OFFSET 0x4000
#define CHIP_SIZE 4
#define RAM_OFFSET 0x8000
#define RAM_SIZE 0x8000
#define RAM_MASK 0x7fff

static uae_u8 config[256];
static uae_u8 boardram[RAM_SIZE];
static volatile uae_u16 csr[4];
static int rap;
static int configured;

static struct netdriverdata *td;
static void *sysdata;

static int am_initialized;
static volatile int transmitnow;
static uae_u16 am_mode;
static uae_u64 am_ladrf;
static uae_u32 am_rdr, am_rdr_rlen, am_rdr_rdra;
static uae_u32 am_tdr, am_tdr_tlen, am_tdr_tdra;
static int tdr_offset, rdr_offset;
static int dbyteswap, prom, fakeprom;
static uae_u8 fakemac[6], realmac[6];
static uae_u8 broadcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#define CSR0_ERR 0x8000
#define CSR0_BABL 0x4000
#define CSR0_CERR 0x2000
#define CSR0_MISS 0x1000
#define CSR0_MERR 0x0800
#define CSR0_RINT 0x0400
#define CSR0_TINT 0x0200
#define CSR0_IDON 0x0100
#define CSR0_INTR 0x0080
#define CSR0_INEA 0x0040
#define CSR0_RXON 0x0020
#define CSR0_TXON 0x0010
#define CSR0_TDMD 0x0008
#define CSR0_STOP 0x0004
#define CSR0_STRT 0x0002
#define CSR0_INIT 0x0001

#define CSR3_BSWP 0x0004
#define CSR3_ACON 0x0002
#define CSR3_BCON 0x0001

#define MODE_PROM 0x8000
#define MODE_EMBA 0x0080
#define MODE_INTL 0x0040
#define MODE_DRTY 0x0020
#define MODE_COLL 0x0010
#define MODE_DTCR 0x0008
#define MODE_LOOP 0x0004
#define MODE_DTX  0x0002
#define MODE_DRX  0x0001

#define TX_OWN 0x8000
#define TX_ERR 0x4000
#define TX_ADD_FCS 0x2000
#define TX_MORE 0x1000
#define TX_ONE 0x0800
#define TX_DEF 0x0400
#define TX_STP 0x0200
#define TX_ENP 0x0100

#define TX_BUFF 0x8000
#define TX_UFLO 0x4000
#define TX_LCOL 0x1000
#define TX_LCAR 0x0800
#define TX_RTRY 0x0400

#define RX_OWN 0x8000
#define RX_ERR 0x4000
#define RX_FRAM 0x2000
#define RX_OFLO 0x1000
#define RX_CRC 0x0800
#define RX_BUFF 0x0400
#define RX_STP 0x0200
#define RX_ENP 0x0100

DECLARE_MEMORY_FUNCTIONS(a2065);

static uae_u16 gword2 (uae_u8 *p)
{
	return (p[0] << 8) | p[1];
}
static uae_u16 gword (uae_u8 *p)
{
	return (p[0] << 8) | p[1];
}
static void pword (uae_u8 *p, uae_u16 v)
{
	p[0] = v >> 8;
	p[1] = v;
}

static void ew (int addr, uae_u32 value)
{
	addr &= 0xffff;
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		config[addr] = (value & 0xf0);
		config[addr + 2] = (value & 0x0f) << 4;
	} else {
		config[addr] = ~(value & 0xf0);
		config[addr + 2] = ~((value & 0x0f) << 4);
	}
}

void a2065_reset (void)
{
	am_initialized = 0;
	csr[0] = CSR0_STOP;
	csr[1] = csr[2] = csr[3] = 0;
	dbyteswap = 0;
	rap = 0;

	ethernet_close (td, sysdata);
	xfree (sysdata);
	sysdata = NULL;
	td = NULL;
}

#if DUMPPACKET
static void dumppacket (const TCHAR *n, uae_u8 *packet, int len)
{
	int i;
	TCHAR buf[10000];

	for (i = 0; i < len; i++) {
		_stprintf (buf + i * 3, _T(".%02X"), packet[i]);
	}
	write_log (_T("%s %d: "), n, len);
	write_log (_T("%s"), buf);
	write_log (_T("\n\n"));
}
#endif

#define MAX_PACKET_SIZE 4000
static uae_u8 transmitbuffer[MAX_PACKET_SIZE];
static volatile int transmitlen;

static int dofakemac (uae_u8 *packet)
{
	if (!memcmp (packet, fakemac, 6)) {
		memcpy (packet, realmac, 6);
		return 1;
	}
	if (!memcmp (packet, realmac, 6)) {
		memcpy (packet, fakemac, 6);
		return 1;
	}
	return 0;
}

// Replace card's MAC with real MAC and vice versa.
// We have to do this because drivers are hardcoded to
// Commodore's MAC address range.

static int mungepacket (uae_u8 *packet, int len)
{
	uae_u8 *data;
	uae_u16 type;
	int ret = 0;

	if (len < 20)
		return 0;
#if DUMPPACKET
	dumppacket (_T("pre:"), packet, len);
#endif
	data = packet + 14;
	type = (packet[12] << 8) | packet[13];
	// switch destination mac
	ret |= dofakemac (packet);
	// switch source mac
	ret |= dofakemac (packet + 6);
	if (type == 0x0806) { // ARP?
		if (((data[0] << 8) | data[1]) == 1 && data[4] == 6) { // Ethernet and LEN=6?
			ret |= dofakemac (data + 8); // sender
			ret |= dofakemac (data + 8 + 6 + 4); // target
		}
	} else if (type == 0x0800) { // IPv4?
		int proto = data[9];
		int ihl = data[0] & 15;
		uae_u8 *ipv4 = data;

		data += ihl * 4;
		if (proto == 17) { // UDP?
			int udpcrc = 0;
			int sp = (data[0] << 8) | data[1];
			int dp = (data[2] << 8) | data[3];
			int len = (data[4] << 8) | data[5];
			if (sp == 67 || sp == 68 || dp == 67 || dp == 68)
				udpcrc |= dofakemac (data + 36); // DHCP CHADDR
			if (udpcrc && (data[6] || data[7])) {
				// fix UDP checksum
				int i;
				uae_u32 sum;
				data[6] = data[7] = 0;
				data[len] = 0;
				sum = 0;
				for (i = 0; i < ((len + 1) & ~1); i += 2)
					sum += (data[i] << 8) | data[i + 1];
				sum += (ipv4[12] << 8) | ipv4[13];
				sum += (ipv4[14] << 8) | ipv4[15];
				sum += (ipv4[16] << 8) | ipv4[17];
				sum += (ipv4[18] << 8) | ipv4[19];
				sum += 17;
				sum += len;
				while (sum >> 16)
					sum = (sum & 0xFFFF) + (sum >> 16);
				sum = ~sum;
				if (sum == 0)
					sum = 0xffff;
				data[6] = sum >> 8;
				data[7] = sum >> 0;	
				ret |= 1;
			}
			// this all just to translate single DHCP MAC..
		}
	}
#if DUMPPACKET
	dumppacket (_T("post:"), packet, len);
#endif
	return ret;
}

static int mcfilter (const uae_u8 *data)
{
	if (am_ladrf == 0) // multicast filter completely disabled?
		return 0;
	return 1; // just allow everything
}

static void gotfunc (void *devv, const uae_u8 *databuf, int len)
{
	int i;
	int size, insize, first;
	uae_u32 addr;
	uae_u8 *p, *d;
	uae_u16 rmd0, rmd1, rmd2, rmd3;
	uae_u32 crc32;
	uae_u8 tmp[MAX_PACKET_SIZE], *data;
	const uae_u8 *dstmac, *srcmac;
	struct s2devstruct *dev = (struct s2devstruct*)devv;

	if (log_a2065 > 1 && log_receive) {
		dstmac = databuf;
		srcmac = databuf + 6;
		write_log (_T("A2065<!DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n"),
			dstmac[0], dstmac[1], dstmac[2], dstmac[3], dstmac[4], dstmac[5],
			srcmac[6], srcmac[7], srcmac[8], srcmac[9], srcmac[10], srcmac[11],
			(databuf[12] << 8) | databuf[13], len);
	}

	if (!(csr[0] & CSR0_RXON)) // receiver off?
		return;
	if (len < 20) { // too short
		if (log_a2065)
			write_log (_T("A2065: short frame, %d bytes\n"), len);
		return;
	}

	dstmac = databuf;
	srcmac = databuf + 6;

	if ((dstmac[0] & 0x01) && memcmp (dstmac, broadcast, sizeof broadcast) != 0) {
		// multicast
		if (!mcfilter (dstmac)) {
			if (log_a2065 > 1)
				write_log (_T("mc filtered\n"));
			return;
		}
	} else {
		// !promiscuous and dst != me and dst != broadcast
		if (!prom && (memcmp (dstmac, realmac, sizeof realmac) != 0 && memcmp (dstmac, broadcast, sizeof broadcast) != 0)) {
			if (log_a2065 > 1)
				write_log (_T("not for me1\n"));
			return;
		}
	}

	// src and dst = me? right, better drop it.
	if (memcmp (dstmac, realmac, sizeof realmac) == 0 && memcmp (srcmac, realmac, sizeof realmac) == 0) {
		if (log_a2065 > 1)
			write_log (_T("not for me2\n"));
		return;
	}
	// dst = broadcast and src = me? no thanks.
	if (memcmp (dstmac, broadcast, sizeof broadcast) == 0 && memcmp (srcmac, realmac, sizeof realmac) == 0) {
		if (log_a2065 > 1)
			write_log (_T("not for me3\n"));
		return;
	}

	memcpy (tmp, databuf, len);
#if 0
	FILE *f = fopen("s:\\d\\wireshark2.cap", "rb");
	fseek (f, 474, SEEK_SET);
	fread (tmp, 342, 1, f);
	fclose (f);
	realmac[0] = 0xc8;
	realmac[1] = 0x0a;
	realmac[2] = 0xa9;
	realmac[3] = 0x81;
	realmac[4] = 0xff;
	realmac[5] = 0x2f;
	fakemac[3] = realmac[3];
	fakemac[4] = realmac[4];
	fakemac[5] = realmac[5];
#endif
	d = tmp;
	dstmac = d;
	srcmac = d + 6;
	if (log_a2065 && log_receive) {
		if (memcmp (dstmac, realmac, sizeof realmac) == 0) {
			write_log (_T("A2065<-DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n"),
				dstmac[0], dstmac[1], dstmac[2], dstmac[3], dstmac[4], dstmac[5],
				srcmac[6], srcmac[7], srcmac[8], srcmac[9], srcmac[10], srcmac[11],
				(d[12] << 8) | d[13], len);
		}
	}
	if (mungepacket (d, len)) {
		if (log_a2065 && log_receive) {
			write_log (_T("A2065<*DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n"),
				dstmac[0], dstmac[1], dstmac[2], dstmac[3], dstmac[4], dstmac[5],
				srcmac[6], srcmac[7], srcmac[8], srcmac[9], srcmac[10], srcmac[11],
				(d[12] << 8) | d[13], len);
		}
	}

	// winpcap does not include checksum bytes
	crc32 = get_crc32 (d, len);
	d[len++] = crc32 >> 24;
	d[len++] = crc32 >> 16;
	d[len++] = crc32 >>  8;
	d[len++] = crc32 >>  0;
	data = tmp;

	size = 0;
	insize = 0;
	first = 1;

	for (;;) {
		rdr_offset %= am_rdr_rlen;
		p = boardram + ((am_rdr_rdra + rdr_offset * 8) & RAM_MASK);
		rmd0 = gword (p + 0);
		rmd1 = gword (p + 2);
		rmd2 = gword (p + 4);
		rmd3 = gword (p + 6);
		addr = rmd0 | ((rmd1 & 0xff) << 16);
		addr &= RAM_MASK;

		if (!(rmd1 & RX_OWN)) {
			write_log (_T("A2065: RECEIVE BUFFER ERROR\n"));
			if (!first) {
				rmd1 |= RX_BUFF | RX_OFLO;
				csr[0] &= ~CSR0_RXON;
			} else {
				csr[0] |= CSR0_MISS;
			}
			pword (p + 2, rmd1);
			rethink_a2065 ();
			return;
		}

		rmd1 &= ~RX_OWN;
		rdr_offset++;

		if (first) {
			rmd1 |= RX_STP;
			first = 0;
		}

		size = 65536 - rmd2;
		for (i = 0; i < size && insize < len; i++, insize++)
			boardram[((addr + i) ^ 0) & RAM_MASK] = data[insize];
		if (insize >= len) {
			rmd1 |= RX_ENP;
			rmd3 = len;
		}

		pword (p + 2, rmd1);
		pword (p + 6, rmd3);

		if (insize >= len)
			break;
	}

	csr[0] |= CSR0_RINT;
	rethink_a2065 ();
}

static int getfunc (void *devv, uae_u8 *d, int *len)
{
	struct s2devstruct *dev = (struct s2devstruct*)devv;

	if (transmitlen <= 0)
		return 0;
	if (transmitlen > *len) {
		write_log (_T("A2065: too large packet transmission attempt %d > %d\n"), transmitlen, *len);
		transmitlen = 0;
		return 0;
	}
	memcpy (d, transmitbuffer, transmitlen);
	*len = transmitlen;
	transmitlen = 0;
	transmitnow = 1;
	return 1;
}

static void do_transmit (void)
{
	int i;
	int size, outsize;
	int err, add_fcs;
	uae_u32 addr, bufaddr;
	uae_u8 *p;
	uae_u16 tmd0, tmd1, tmd2, tmd3;

	err = 0;
	size = 0;
	outsize = 0;

	tdr_offset %= am_tdr_tlen;
	bufaddr = am_tdr_tdra + tdr_offset * 8;
	p = boardram + (bufaddr & RAM_MASK);
	tmd1 = gword (p + 2);
	if (!(tmd1 & TX_OWN) || !(tmd1 & TX_STP)) {
		tdr_offset++;
		return;
	}
	if (!(tmd1 & TX_ENP) && log_a2065 > 0)
		write_log (_T("A2065: chained transmit!?\n"));

	add_fcs = tmd1 & TX_ADD_FCS;

	for (;;) {
		tdr_offset %= am_tdr_tlen;
		p = boardram + ((am_tdr_tdra + tdr_offset * 8) & RAM_MASK);
		tmd0 = gword (p + 0);
		tmd1 = gword (p + 2);
		tmd2 = gword (p + 4);
		tmd3 = gword (p + 6);
		addr = tmd0 | ((tmd1 & 0xff) << 16);
		addr &= RAM_MASK;

		if (!(tmd1 & TX_OWN)) {
			tmd3 |= TX_BUFF | TX_UFLO;
			tmd1 |= TX_ERR;
			csr[0] &= ~CSR0_TXON;
			write_log (_T("A2065: TRANSMIT OWN NOT SET\n"));
			err = 1;
		} else {
			tmd1 &= ~TX_OWN;
			size = 65536 - tmd2;
			if (size > MAX_PACKET_SIZE)
				size = MAX_PACKET_SIZE;
			for (i = 0; i < size; i++)
				transmitbuffer[outsize++] = boardram[((addr + i) ^ 0) & RAM_MASK];
			tdr_offset++;
		}
		pword (p + 2, tmd1);
		pword (p + 6, tmd3);
		if ((tmd1 & TX_ENP) || err)
			break;
	}
	if (outsize < 60) {
		tmd3 |= TX_BUFF | TX_UFLO;
		tmd1 |= TX_ERR;
		csr[0] &= ~CSR0_TXON;
		write_log (_T("A2065: TRANSMIT UNDERFLOW %d\n"), outsize);
		err = 1;
		pword (p + 2, tmd1);
		pword (p + 6, tmd3);
	}

	if (!err) {
		uae_u8 *d = transmitbuffer;
		if ((am_mode & MODE_DTCR) && !add_fcs)
			outsize -= 4; // do not include checksum bytes
		if (log_a2065 && log_transmit) {
			write_log (_T("A2065->DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d ADDR=%04X\n"),
				d[0], d[1], d[2], d[3], d[4], d[5],
				d[6], d[7], d[8], d[9], d[10], d[11],
				(d[12] << 8) | d[13], outsize, bufaddr);
		}
		transmitlen = outsize;
		if (mungepacket (d, transmitlen)) {
			if (log_a2065 && log_transmit) {
				write_log (_T("A2065*>DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n"),
					d[0], d[1], d[2], d[3], d[4], d[5],
					d[6], d[7], d[8], d[9], d[10], d[11],
					(d[12] << 8) | d[13], outsize);
			}
		}
		ethernet_trigger (td, sysdata);
	}
	csr[0] |= CSR0_TINT;
	rethink_a2065 ();
}

static void check_transmit (void)
{
	if (transmitlen > 0)
		return;
	if (!(csr[0] & CSR0_TXON))
		return;
	transmitnow = 0;
	do_transmit ();
}

void a2065_hsync_handler (void)
{
	static int cnt;

	cnt--;
	if (cnt < 0 || transmitnow) {
		check_transmit ();
		cnt = 15;
	}
}

void rethink_a2065 (void)
{
	bool was = (uae_int_requested & 4) != 0;
	uae_int_requested &= ~4;
	if (!configured)
		return;
	csr[0] &= ~CSR0_INTR;
	if (csr[0] & (CSR0_BABL | CSR0_MISS | CSR0_MERR | CSR0_RINT | CSR0_TINT | CSR0_IDON))
		csr[0] |= CSR0_INTR;
	if ((csr[0] & (CSR0_INTR | CSR0_INEA)) == (CSR0_INTR | CSR0_INEA)) {
		uae_int_requested |= 4;
		if (!was && log_a2065 > 2)
			write_log(_T("A2065 +IRQ\n"));
	}
	if (log_a2065 && was && !(uae_int_requested & 4)) {
		write_log(_T("A2065 -IRQ\n"));
	}
}

static void chip_init (void)
{
	uae_u32 iaddr = ((csr[2] & 0xff) << 16) | csr[1];
	uae_u8 *p = boardram + (iaddr & RAM_MASK);

	write_log (_T("A2065: Initialization block2:\n"));
	for (int i = 0; i < 24; i++)
		write_log (_T(".%02X"), p[i]);
	write_log (_T("\n"));

	am_mode = gword2 (p + 0);
	am_ladrf = (((uae_u64)gword2 (p + 14)) << 48) | (((uae_u64)gword2 (p + 12)) << 32) | (((uae_u64)gword2 (p + 10)) << 16) | gword2 (p + 8);
	am_rdr = (gword2 (p + 18) << 16) | gword2 (p + 16);
	am_tdr = (gword2 (p + 22) << 16) | gword2 (p + 20);

	am_rdr_rlen = 1 << ((am_rdr >> 29) & 7);
	am_tdr_tlen = 1 << ((am_tdr >> 29) & 7);
	am_rdr_rdra = am_rdr & 0x00fffff8;
	am_tdr_tdra = am_tdr & 0x00fffff8;

	prom = (am_mode & MODE_PROM) ? 1 : 0;
	fakeprom = a2065_promiscuous ? 1 : 0;
	
	fakemac[0] = p[3];
	fakemac[1] = p[2];
	fakemac[2] = p[5];
	fakemac[3] = p[4];
	fakemac[4] = p[7];
	fakemac[5] = p[6];

	write_log (_T("A2065: %04X %06X %d %d %d %d %06X %06X %02X:%02X:%02X:%02X:%02X:%02X\n"),
		am_mode, iaddr, prom, fakeprom, am_rdr_rlen, am_tdr_tlen, am_rdr_rdra, am_tdr_tdra,
		fakemac[0], fakemac[1], fakemac[2], fakemac[3], fakemac[4], fakemac[5]);

	am_rdr_rdra &= RAM_MASK;
	am_tdr_tdra &= RAM_MASK;
	tdr_offset = rdr_offset = 0;

	ethernet_close (td, sysdata);
	if (td != NULL) {
		if (!sysdata)
			sysdata = xcalloc (uae_u8, ethernet_getdatalenght (td));
		if (!ethernet_open (td, sysdata, NULL, gotfunc, getfunc, prom || fakeprom)) {
			write_log (_T("A2065: failed to initialize winpcap driver\n"));
		}
	}
}

static uae_u16 chip_wget (uaecptr addr)
{
	if (addr == RAP) {
		return rap;
	} else if (addr == RDP) {
		uae_u16 v = csr[rap];
		if (rap == 0) {
			if (v & (CSR0_BABL | CSR0_CERR | CSR0_MISS | CSR0_MERR))
				v |= CSR0_ERR;
		}
		if (log_a2065 > 2)
			write_log (_T("A2065_CHIPWGET: CSR%d=%04X PC=%08X\n"), rap, v, M68K_GETPC);
		return v;
	}
	return 0xffff;
}

static void chip_wput (uaecptr addr, uae_u16 v)
{

	if (addr == RAP) {

		rap = v & 3;

	} else if (addr == RDP) {

		uae_u16 oreg = csr[rap];
		uae_u16 t;

		if (log_a2065 > 2)
			write_log (_T("A2065_CHIPWPUT: CSR%d=%04X PC=%08X\n"), rap, v & 0xffff, M68K_GETPC);

		switch (rap)
		{
		case 0:
			csr[0] &= ~CSR0_INEA; csr[0] |= v & CSR0_INEA;
			// bit = 1 -> set, bit = 0 -> nop
			t = v & (CSR0_INIT | CSR0_STRT | CSR0_STOP | CSR0_TDMD);
			csr[0] |= t;
			// bit = 1 -> clear, bit = 0 -> nop
			t = v & (CSR0_IDON | CSR0_TINT | CSR0_RINT | CSR0_MERR | CSR0_MISS | CSR0_CERR | CSR0_BABL);
			csr[0] &= ~t;
			csr[0] &= ~CSR0_ERR;

			if ((csr[0] & CSR0_STOP) && !(oreg & CSR0_STOP)) {

				csr[0] = CSR0_STOP;
				if (log_a2065)
					write_log (_T("A2065: STOP. %04X -> %04X -> %04X\n"), oreg, v, csr[0]);
				csr[3] = 0;
				dbyteswap = 0;

			} else if ((csr[0] & CSR0_STRT) && !(oreg & CSR0_STRT) && (oreg & (CSR0_STOP | CSR0_INIT))) {

				csr[0] &= ~CSR0_STOP;
				if (!(am_mode & MODE_DTX))
					csr[0] |= CSR0_TXON;
				if (!(am_mode & MODE_DRX))
					csr[0] |= CSR0_RXON;
				if ((csr[0] & CSR0_INIT) && !(oreg & CSR0_INIT)) {
					chip_init ();
					csr[0] |= CSR0_IDON;
					am_initialized = 1;
					if (log_a2065)
						write_log (_T("A2065: INIT+START. %04X -> %04X -> %04X\n"), oreg, v, csr[0]);
				}
				if (log_a2065)
					write_log (_T("A2065: START. %04X -> %04X -> %04X\n"), oreg, v, csr[0]);
			
			} else if ((csr[0] & CSR0_INIT) && !(oreg & CSR0_INIT) && (oreg & CSR0_STOP)) {

				chip_init ();
				csr[0] |= CSR0_IDON;
				csr[0] &= ~(CSR0_RXON | CSR0_TXON | CSR0_STOP);
				am_initialized = 1;
				csr[3] = 0;
				if (log_a2065)
					write_log (_T("A2065: INIT. %04X -> %04X -> %04X\n"), oreg, v, csr[0]);
			}

			if ((csr[0] & CSR0_STRT) && am_initialized) {
				if (csr[0] & CSR0_TDMD)
					check_transmit ();
			}
			csr[0] &= ~CSR0_TDMD;

			rethink_a2065 ();
			break;
		case 1:
			if (csr[0] & 4) {
				csr[1] = v;
				csr[1] &= ~1;
			}
			break;
		case 2:
			if (csr[0] & 4) {
				csr[2] = v;
				csr[2] &= 0x00ff;
			}
			break;
		case 3:
			if (csr[0] & 4) {
				csr[3] = v;
				csr[3] &= 7;
			}
			dbyteswap = 0;
			/*
			 * Some drivers set this but only work if no byteswapping
			 * is done. Weird..
			 * dbyteswap = (csr[3] & CSR3_BSWP) ? 1 : 0;
			*/
			break;

		}
	}
}


static uae_u32 a2065_bget2 (uaecptr addr)
{
	uae_u32 v = 0;

	if (addr >= RAM_OFFSET) {
		v = boardram[(addr & RAM_MASK)];
	}
	return v;
}

static void a2065_bput2 (uaecptr addr, uae_u32 v)
{
	if (addr >= RAM_OFFSET) {
		boardram[(addr & RAM_MASK)] = v;
	}
}

static uae_u32 REGPARAM2 a2065_wget (uaecptr addr)
{
	uae_u32 v;
	addr &= 65535;
	if (addr == CHIP_OFFSET || addr == CHIP_OFFSET + 2) {
		v = chip_wget (addr);
	} else {
#if 1
		v = a2065_bget2 (addr + 0) << 8;
		v |= a2065_bget2 (addr + 1);
#else
		v = a2065_bget2 (addr + 1) << 8;
		v |= a2065_bget2 (addr + 0);
#endif
	} 
	if (log_a2065 > 3 && addr < MEM_MIN)
		write_log (_T("A2065_WGET: %08X -> %04X PC=%08X\n"), addr, v & 0xffff, M68K_GETPC);
	return v;
}

static uae_u32 REGPARAM2 a2065_lget (uaecptr addr)
{
	uae_u32 v;
	addr &= 65535;
	v = a2065_wget (addr) << 16;
	v |= a2065_wget (addr + 2);
	return v;
}

static uae_u32 REGPARAM2 a2065_bget (uaecptr addr)
{
	uae_u32 v;
	addr &= 65535;
	if (addr < 0x40) {
		v = config[addr];
	} else {
		if (!configured)
			return 0;
		v = a2065_bget2 (addr ^ 0);
	}
	if (log_a2065 > 3 && addr < MEM_MIN)
		write_log (_T("A2065_BGET: %08X -> %02X PC=%08X\n"), addr, v & 0xff, M68K_GETPC);
	return v;
}

static void REGPARAM2 a2065_wput (uaecptr addr, uae_u32 w)
{
	addr &= 65535;
	if (addr == CHIP_OFFSET || addr == CHIP_OFFSET + 2) {
		chip_wput (addr, w);
	} else {
#if 1
		a2065_bput2 (addr, w >> 8);
		a2065_bput2 (addr + 1, w);
#else
		a2065_bput2 (addr + 1, w >> 8);
		a2065_bput2 (addr + 0, w);
#endif
	}
	if (log_a2065 > 3 && addr < MEM_MIN)
		write_log (_T("A2065_WPUT: %08X <- %04X PC=%08X\n"), addr, w & 0xffff, M68K_GETPC);
}

static void REGPARAM2 a2065_lput (uaecptr addr, uae_u32 l)
{
	addr &= 65535;
	a2065_wput (addr, l >> 16);
	a2065_wput (addr + 2, l);
}

uae_u8 *REGPARAM2 a2065_xlate(uaecptr addr)
{
	if ((addr & 65535) >= RAM_OFFSET)
		return &boardram[addr & RAM_MASK];
	return default_xlate(addr);
}

int REGPARAM2 a2065_check(uaecptr a, uae_u32 b)
{
	a &= 65535;
	return a >= RAM_OFFSET && a + b < 65536;
}

static addrbank a2065_bank = {
	a2065_lget, a2065_wget, a2065_bget,
	a2065_lput, a2065_wput, a2065_bput,
	a2065_xlate, a2065_check, NULL, NULL, _T("A2065 Z2 Ethernet"),
	a2065_lgeti, a2065_wgeti,
	ABFLAG_IO, S_READ, S_WRITE
};

static void REGPARAM2 a2065_bput (uaecptr addr, uae_u32 b)
{
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48 && !configured) {
		map_banks_z2 (&a2065_bank, b, 0x10000 >> 16);
		configured = b;
		expamem_next(&a2065_bank, NULL);
		return;
	}
	if (addr == 0x4c && !configured) {
		configured = 0xff;
		expamem_shutup(&a2065_bank);
		return;
	}
	if (!configured)
		return;
	if (log_a2065 > 3 && addr < MEM_MIN)
		write_log (_T("A2065_BPUT: %08X <- %02X PC=%08X\n"), addr, b & 0xff, M68K_GETPC);
	a2065_bput2 (addr ^ 0, b);
}

static uae_u32 REGPARAM2 a2065_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
	addr &= 65535;
	return v;
}
static uae_u32 REGPARAM2 a2065_lgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
	addr &= 65535;
	v = (a2065_wgeti (addr) << 16) | a2065_wgeti (addr + 2);
	return v;
}

static addrbank *a2065_config (void)
{
	memset (config, 0xff, sizeof config);
	ew (0x00, 0xc0 | 0x01);
	// hardware id
	ew (0x04, 0x70);
	// manufacturer (Commodore)
	ew (0x10, 0x02);
	ew (0x14, 0x02);

	td = NULL;
	if (ethernet_enumerate (&td, currprefs.a2065name)) {
		memcpy (realmac, td->mac, sizeof realmac);
		if (!td->mac[0] && !td->mac[1] && !td->mac[2]) {
			realmac[0] = 0x00;
			realmac[1] = 0x80;
			realmac[2] = 0x10;
		}
		write_log (_T("A2065: '%s' %02X:%02X:%02X:%02X:%02X:%02X\n"),
			td->name, td->mac[0], td->mac[1], td->mac[2], td->mac[3], td->mac[4], td->mac[5]);
	} else {
		realmac[0] = 0x00;
		realmac[1] = 0x80;
		realmac[2] = 0x10;
		realmac[3] = 4;
		realmac[4] = 3;
		realmac[5] = 2;
		write_log (_T("A2065: Disconnected mode %02X:%02X:%02X:%02X:%02X:%02X\n"),
			realmac[0], realmac[1], realmac[2], realmac[3], realmac[4], realmac[5]);
	}

	ew (0x18, realmac[2]);
	ew (0x1c, realmac[3]);
	ew (0x20, realmac[4]);
	ew (0x24, realmac[5]);

	fakemac[0] = 0x00;
	fakemac[1] = 0x80;
	fakemac[2] = 0x10;
	fakemac[3] = realmac[3];
	fakemac[4] = realmac[4];
	fakemac[5] = realmac[5];

	if (configured) {
		if (configured != 0xff)
			map_banks_z2 (&a2065_bank, configured, 0x10000 >> 16);
	} else {
		/* KS autoconfig handles the rest */
		return &a2065_bank;
	}
	return NULL;
}

uae_u8 *save_a2065 (int *len, uae_u8 *dstptr)
{
	uae_u8 *dstbak,*dst;

	if (currprefs.a2065name[0] == 0)
		return NULL;
	if (dstptr)
		dstbak = dst = dstptr;
	else
		dstbak = dst = (uae_u8*)malloc (16);
	save_u32 (0);
	save_u8 (configured);
	for (int i = 0; i < 6; i++)
		save_u8 (realmac[i]);
	*len = dst - dstbak;
	return dstbak;
}
uae_u8 *restore_a2065 (uae_u8 *src)
{
	restore_u32 ();
	configured = restore_u8 ();
	for (int i = 0; i < 6; i++)
		realmac[i] = restore_u8 ();
	return src;
}

void restore_a2065_finish (void)
{
	if (configured)
		a2065_config ();
}

addrbank *a2065_init (int devnum)
{
	configured = 0;
	return a2065_config ();
}

#endif /* A2065 */
