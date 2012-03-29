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

#include "options.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "a2065.h"
#include "win32_uaenet.h"
#include "crc32.h"
#include "savestate.h"
#include "autoconf.h"

#define DUMPPACKET 0

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
static int byteswap, prom, fakeprom;
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
	csr[0] = csr[1] = csr[2] = csr[3] = 0;
	rap = 0;

	uaenet_close (sysdata);
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
	write_log (buf);
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

static void gotfunc (struct s2devstruct *dev, const uae_u8 *databuf, int len)
{
	int i;
	int size, insize, first;
	uae_u32 addr;
	uae_u8 *p, *d;
	uae_u16 rmd0, rmd1, rmd2, rmd3;
	uae_u32 crc32;
	uae_u8 tmp[MAX_PACKET_SIZE], *data;
	const uae_u8 *dstmac, *srcmac;

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
		rmd0 = (p[1] << 8) | (p[0] << 0);
		rmd1 = (p[3] << 8) | (p[2] << 0);
		rmd2 = (p[5] << 8) | (p[4] << 0);
		rmd3 = (p[7] << 8) | (p[6] << 0);
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
			p[3] = rmd1 >> 8;
			p[2] = rmd1 >> 0;
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
			boardram[((addr + i) ^ byteswap) & RAM_MASK] = data[insize];
		if (insize >= len) {
			rmd1 |= RX_ENP;
			rmd3 = len;
		}

		p[3] = rmd1 >> 8;
		p[2] = rmd1 >> 0;
		p[7] = rmd3 >> 8;
		p[6] = rmd3 >> 0;

		if (insize >= len)
			break;
	}

	csr[0] |= CSR0_RINT;
	rethink_a2065 ();
}

static int getfunc (struct s2devstruct *dev, uae_u8 *d, int *len)
{
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
	uae_u32 addr;
	uae_u8 *p;
	uae_u16 tmd0, tmd1, tmd2, tmd3;

	err = 0;
	size = 0;
	outsize = 0;

	tdr_offset %= am_tdr_tlen;
	p = boardram + ((am_tdr_tdra + tdr_offset * 8) & RAM_MASK);
	tmd1 = (p[3] << 8) | (p[2] << 0);
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
		tmd0 = (p[1] << 8) | (p[0] << 0);
		tmd1 = (p[3] << 8) | (p[2] << 0);
		tmd2 = (p[5] << 8) | (p[4] << 0);
		tmd3 = (p[7] << 8) | (p[6] << 0);
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
				transmitbuffer[outsize++] = boardram[((addr + i) ^ byteswap) & RAM_MASK];
			tdr_offset++;
		}
		p[3] = tmd1 >> 8;
		p[2] = tmd1 >> 0;
		p[7] = tmd3 >> 8;
		p[6] = tmd3 >> 0;
		if ((tmd1 & TX_ENP) || err)
			break;
	}
	if (outsize < 60) {
		tmd3 |= TX_BUFF | TX_UFLO;
		tmd1 |= TX_ERR;
		csr[0] &= ~CSR0_TXON;
		write_log (_T("A2065: TRANSMIT UNDERFLOW %d\n"), outsize);
		err = 1;
		p[3] = tmd1 >> 8;
		p[2] = tmd1 >> 0;
		p[7] = tmd3 >> 8;
		p[6] = tmd3 >> 0;
	}

	if (!err) {
		uae_u8 *d = transmitbuffer;
		if ((am_mode & MODE_DTCR) && !add_fcs)
			outsize -= 4; // do not include checksum bytes
		if (log_a2065 && log_transmit) {
			write_log (_T("A2065->DST:%02X.%02X.%02X.%02X.%02X.%02X SRC:%02X.%02X.%02X.%02X.%02X.%02X E=%04X S=%d\n"),
				d[0], d[1], d[2], d[3], d[4], d[5],
				d[6], d[7], d[8], d[9], d[10], d[11],
				(d[12] << 8) | d[13], outsize);
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
		uaenet_trigger (sysdata);
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
	uae_int_requested &= ~4;
	if (!configured)
		return;
	csr[0] &= ~CSR0_INTR;
	if (csr[0] & (CSR0_BABL | CSR0_MISS | CSR0_MERR | CSR0_RINT | CSR0_TINT | CSR0_IDON))
		csr[0] |= CSR0_INTR;
	if ((csr[0] & (CSR0_INTR | CSR0_INEA)) == (CSR0_INTR | CSR0_INEA))
		uae_int_requested |= 4;
}

static void chip_init (void)
{
	uae_u32 iaddr = ((csr[2] & 0xff) << 16) | csr[1];
	uae_u8 *p = boardram + (iaddr & RAM_MASK);

	write_log (_T("A2065: Initialization block2:\n"));
	for (int i = 0; i < 24; i++)
		write_log (_T(".%02X"), p[i]);
	write_log (_T("\n"));

	am_mode = (p[0] << 8) | (p[1] << 0);
	am_ladrf = ((uae_u64)p[15] << 56) | ((uae_u64)p[14] << 48) | ((uae_u64)p[13] << 40) | ((uae_u64)p[12] << 32) | (p[11] << 24) | (p[10] << 16) | (p[9] << 8) | (p[8] << 0);
	am_rdr = (p[19] << 24) | (p[18] << 16) | (p[17] << 8) | (p[16] << 0);
	am_tdr = (p[23] << 24) | (p[22] << 16) | (p[21] << 8) | (p[20] << 0);

	am_rdr_rlen = 1 << ((am_rdr >> 29) & 7);
	am_tdr_tlen = 1 << ((am_tdr >> 29) & 7);
	am_rdr_rdra = am_rdr & 0x00fffff8;
	am_tdr_tdra = am_tdr & 0x00fffff8;

	prom = (am_mode & MODE_PROM) ? 1 : 0;
	fakeprom = a2065_promiscuous ? 1 : 0;
	
	fakemac[0] = p[2];
	fakemac[1] = p[3];
	fakemac[2] = p[4];
	fakemac[3] = p[5];
	fakemac[4] = p[6];
	fakemac[5] = p[7];

	write_log (_T("A2065: %04X %06X %d %d %d %d %06X %06X %02X:%02X:%02X:%02X:%02X:%02X\n"),
		am_mode, iaddr, prom, fakeprom, am_rdr_rlen, am_tdr_tlen, am_rdr_rdra, am_tdr_tdra,
		fakemac[0], fakemac[1], fakemac[2], fakemac[3], fakemac[4], fakemac[5]);


	am_rdr_rdra &= RAM_MASK;
	am_tdr_tdra &= RAM_MASK;
	tdr_offset = rdr_offset = 0;

	uaenet_close (sysdata);
	if (td != NULL) {
		if (!sysdata)
			sysdata = xcalloc (uae_u8, uaenet_getdatalenght());
		if (!uaenet_open (sysdata, td, NULL, gotfunc, getfunc, prom || fakeprom)) {
			write_log (_T("A2065: failed to initialize winpcap driver\n"));
		}
	}
}

static uae_u16 chip_wget (uaecptr addr)
{
	if (addr == RAP) {
		return rap;
	} else if (addr = RDP) {
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

			if ((csr[0] & (CSR0_STOP | CSR0_STRT | CSR0_INIT)) == (CSR0_STOP | CSR0_STRT | CSR0_INIT))
				csr[0] &= ~(CSR0_STRT | CSR0_INIT);
			if (csr[0] & CSR0_INIT)
				csr[0] &= ~CSR0_STOP;

			if ((csr[0] & CSR0_STRT) && !(oreg & CSR0_STRT)) {
				csr[0] &= ~CSR0_STOP;
				if (!(am_mode & MODE_DTX))
					csr[0] |= CSR0_TXON;
				if (!(am_mode & MODE_DRX))
					csr[0] |= CSR0_RXON;
				if (log_a2065)
					write_log (_T("A2065: START.\n"));
			}

			if ((csr[0] & CSR0_STOP) && !(oreg & CSR0_STOP)) {
				csr[0] = CSR0_STOP;
				if (log_a2065)
					write_log (_T("A2065: STOP.\n"));
				csr[3] = 0;
				am_initialized = 0;
			}

			if ((csr[0] & CSR0_INIT) && am_initialized == 0) {
				if (log_a2065)
					write_log (_T("A2065: INIT.\n"));
				chip_init ();
				csr[0] |= CSR0_IDON;
				am_initialized = 1;
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
			byteswap = (csr[3] & CSR3_BSWP) ? 1 : 0;
			break;

		}
	}
}


static uae_u32 a2065_bget2 (uaecptr addr)
{
	uae_u32 v = 0;

	if (addr < 0x40) {
		v = config[addr];
	} else if (addr >= RAM_OFFSET) {
		v = boardram[(addr & RAM_MASK) ^ 1];
	}
	if (log_a2065 > 2)
		write_log (_T("A2065_BGET: %08X -> %02X PC=%08X\n"), addr, v & 0xff, M68K_GETPC);
	return v;
}

static void a2065_bput2 (uaecptr addr, uae_u32 v)
{
	if (addr >= RAM_OFFSET) {
		boardram[(addr & RAM_MASK) ^ 1] = v;
	}
	if (log_a2065 > 2)
		write_log (_T("A2065_BPUT: %08X <- %02X PC=%08X\n"), addr, v & 0xff, M68K_GETPC);
}

static uae_u32 REGPARAM2 a2065_wget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	if (addr == CHIP_OFFSET || addr == CHIP_OFFSET + 2) {
		v = chip_wget (addr);
	} else {
		v = a2065_bget2 (addr) << 8;
		v |= a2065_bget2 (addr + 1);
	} 
	return v;
}

static uae_u32 REGPARAM2 a2065_lget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = a2065_wget (addr) << 16;
	v |= a2065_wget (addr + 2);
	return v;
}

static uae_u32 REGPARAM2 a2065_bget (uaecptr addr)
{
	uae_u32 v;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = a2065_bget2 (addr);
	if (!configured)
		return v;
	return v;
}

static void REGPARAM2 a2065_wput (uaecptr addr, uae_u32 w)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	if (addr == CHIP_OFFSET || addr == CHIP_OFFSET + 2) {
		chip_wput (addr, w);
	} else {
		a2065_bput2 (addr, w >> 8);
		a2065_bput2 (addr + 1, w);
	}
}

static void REGPARAM2 a2065_lput (uaecptr addr, uae_u32 l)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	addr &= 65535;
	a2065_wput (addr, l >> 16);
	a2065_wput (addr + 2, l);
}

extern addrbank a2065_bank;

static void REGPARAM2 a2065_bput (uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	b &= 0xff;
	addr &= 65535;
	if (addr == 0x48 && !configured) {
		map_banks (&a2065_bank, b, 0x10000 >> 16, 0x10000);
		write_log (_T("A2065 Z2 autoconfigured at %02X0000\n"), b);
		configured = b;
		expamem_next ();
		return;
	}
	if (addr == 0x4c && !configured) {
		write_log (_T("A2065 DMAC AUTOCONFIG SHUT-UP!\n"));
		configured = 0xff;
		expamem_next ();
		return;
	}
	if (!configured)
		return;
	a2065_bput2 (addr, b);
}

static uae_u32 REGPARAM2 a2065_wgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	return v;
}
static uae_u32 REGPARAM2 a2065_lgeti (uaecptr addr)
{
	uae_u32 v = 0xffff;
#ifdef JIT
	special_mem |= S_READ;
#endif
	addr &= 65535;
	v = (a2065_wgeti (addr) << 16) | a2065_wgeti (addr + 2);
	return v;
}

static addrbank a2065_bank = {
	a2065_lget, a2065_wget, a2065_bget,
	a2065_lput, a2065_wput, a2065_bput,
	default_xlate, default_check, NULL, _T("A2065 Z2 Ethernet"),
	a2065_lgeti, a2065_wgeti, ABFLAG_IO
};

static void a2065_config (void)
{
	memset (config, 0xff, sizeof config);
	ew (0x00, 0xc0 | 0x01);
	// hardware id
	ew (0x04, 0x70);
	// manufacturer (Commodore)
	ew (0x10, 0x02);
	ew (0x14, 0x02);

	td = NULL;
	if ((td = uaenet_enumerate (NULL, currprefs.a2065name))) {
		memcpy (realmac, td->mac, sizeof realmac);
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
			map_banks (&a2065_bank, configured, 0x10000 >> 16, 0x10000);
	} else {
		/* KS autoconfig handles the rest */
		map_banks (&a2065_bank, 0xe80000 >> 16, 0x10000 >> 16, 0x10000);
	}
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

void a2065_init (void)
{
	configured = 0;
	a2065_config ();
}
