/* Build with VBCC's aos68k target and -lamiga. Run with bsdsocket emulation.
 * Add --bounds to test rejection of overlong/unterminated guest strings.
 * Legacy text results are printed for comparison with the previous build;
 * their interpretation remains the host's native inet_addr implementation.
 */
#include <exec/types.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ipv4_text(__reg("a6") struct Library *, __reg("d0") ULONG) = "\tjsr\t-174(a6)";
ULONG ipv4_addr(__reg("a6") struct Library *, __reg("a0") char *) = "\tjsr\t-180(a6)";
ULONG ipv4_network(__reg("a6") struct Library *, __reg("a0") char *) = "\tjsr\t-204(a6)";
LONG socket_errno(__reg("a6") struct Library *) = "\tjsr\t-162(a6)";
LONG close_socket(__reg("a6") struct Library *, __reg("d0") LONG) = "\tjsr\t-120(a6)";

LONG tcp_socket(__reg("a6") struct Library *, __reg("d0") LONG,
	__reg("d1") LONG, __reg("d2") LONG) = "\tjsr\t-30(a6)";
LONG tcp_connect(__reg("a6") struct Library *, __reg("d0") LONG,
	__reg("a0") void *, __reg("d1") LONG) = "\tjsr\t-54(a6)";
LONG tcp_send(__reg("a6") struct Library *, __reg("d0") LONG,
	__reg("a0") void *, __reg("d1") LONG, __reg("d2") LONG) = "\tjsr\t-66(a6)";
LONG tcp_recv(__reg("a6") struct Library *, __reg("d0") LONG,
	__reg("a0") void *, __reg("d1") LONG, __reg("d2") LONG) = "\tjsr\t-78(a6)";

static LONG failures, checks;
static void check(int ok, const char *name)
{
	checks++;
	if (!ok) {
		failures++;
		Printf("FAIL: %s\n", name);
	}
}

static void roundtrip(struct Library *base, ULONG address)
{
	char expected[16];
	char *text;
	sprintf(expected, "%lu.%lu.%lu.%lu", address >> 24,
		(address >> 16) & 255, (address >> 8) & 255, address & 255);
	text = ipv4_text(base, address);
	check(text && !strcmp(text, expected), "dotted decimal formatting");
	if (text) {
		check(ipv4_addr(base, text) == address, "address byte order");
		check(ipv4_network(base, text) == address, "network alias");
	}
}

/* Optional --echo=PORT connects to a host TCP echo server using the
 * converted address. Amiga sockaddr fields are already in network order.
 */
static void echo_test(struct Library *base, int port)
{
	struct {
		UBYTE length, family;
		UWORD port;
		ULONG address;
		UBYTE padding[8];
	} endpoint;
	UBYTE sent[256], received[256];
	LONG fd, n, total;
	int i;
	memset(&endpoint, 0, sizeof endpoint);
	endpoint.length = sizeof endpoint;
	endpoint.family = 2;
	endpoint.port = port;
	endpoint.address = ipv4_addr(base, "127.0.0.1");
	fd = tcp_socket(base, 2, 1, 0);
	check(fd >= 0, "TCP socket");
	if (fd < 0)
		return;
	n = tcp_connect(base, fd, &endpoint, sizeof endpoint);
	check(n == 0, "TCP connect using parsed address");
	if (!n) {
		for (i = 0; i < sizeof sent; i++)
			sent[i] = i;
		total = 0;
		while (total < sizeof sent) {
			n = tcp_send(base, fd, sent + total, sizeof sent - total, 0);
			if (n <= 0)
				break;
			total += n;
		}
		check(total == sizeof sent, "TCP send");
		total = 0;
		while (total < sizeof received) {
			n = tcp_recv(base, fd, received + total,
				sizeof received - total, 0);
			if (n <= 0)
				break;
			total += n;
		}
		check(total == sizeof received && !memcmp(sent, received, sizeof sent),
			"TCP echo payload");
	}
	close_socket(base, fd);
}

int main(int argc, char **argv)
{
	static const ULONG addresses[] = {
		0, 1, 0x01020304UL, 0x7f000001UL, 0x80000000UL,
		0xc0a80101UL, 0xe0000001UL, 0xffffff00UL, 0xffffffffUL
	};
	static char *legacy[] = {
		"127.1", "10.2.3", "0x7f000001", "0177.0.0.1", "2130706433",
		"0", "4294967295", "0xffffffff", "", " ", "\t127.0.0.1",
		"1.2.3.4 ", "1.2.3.4 junk", "256.1.1.1", "1.2.3.256",
		"1.2.3", "1.2.3.4.5", "1..2", "-1", "1.2.3.4\n",
		"localhost", "::1", "1.2.3.a", "4294967296", "09", NULL
	};
	struct Library *base = OpenLibrary("bsdsocket.library", 4);
	struct Library *second;
	char *first, *other;
	char boundary[256], overlong[1024], unterminated[256];
	ULONG random = 0x12345678UL;
	ULONG value, network;
	LONG before;
	int i, shift, bounds = 0, port = 0;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--bounds")) bounds = 1;
		if (!strncmp(argv[i], "--echo=", 7)) port = atoi(argv[i] + 7);
	}
	if (!base) {
		PutStr("FAIL: no bsdsocket.library\n");
		return 20;
	}
	for (i = 0; i < sizeof addresses / sizeof addresses[0]; i++)
		roundtrip(base, addresses[i]);
	for (shift = 0; shift < 32; shift += 8) {
		for (i = 0; i < 256; i++)
			roundtrip(base, (ULONG)i << shift);
	}
	for (i = 0; i < 256; i++) {
		random = random * 1664525UL + 1013904223UL;
		roundtrip(base, random);
	}
	first = ipv4_text(base, 0xffffffffUL);
	other = ipv4_text(base, 0);
	check(first == other && !strcmp(first, "0.0.0.0"), "scratch reuse and NUL");
	second = OpenLibrary("bsdsocket.library", 4);
	check(second != NULL, "second library base");
	if (second) {
		first = ipv4_text(base, 0x01020304UL);
		other = ipv4_text(second, 0x05060708UL);
		check(first != other && !strcmp(first, "1.2.3.4") &&
			!strcmp(other, "5.6.7.8"), "separate scratch buffers");
		CloseLibrary(second);
	}
	close_socket(base, -1);
	before = socket_errno(base);
	for (i = 0; legacy[i]; i++) {
		value = ipv4_addr(base, legacy[i]);
		network = ipv4_network(base, legacy[i]);
		Printf("legacy %ld %08lx %08lx\n", (LONG)i, value, network);
		check(value == network, "legacy network alias");
		check(socket_errno(base) == before, "parser preserves errno");
	}
	ipv4_text(base, 0x80000001UL);
	check(socket_errno(base) == before, "formatter preserves errno");
	memset(boundary, ' ', sizeof boundary);
	memcpy(boundary, "127.0.0.1", 9);
	boundary[255] = 0;
	check(ipv4_addr(base, boundary) == 0x7f000001UL, "255-byte string");
	check(ipv4_network(base, boundary) == 0x7f000001UL, "255-byte network string");
	check(ipv4_addr(base, (char *)0x30000000UL) == 0, "unmapped guest address");
	check(ipv4_network(base, (char *)0x30000000UL) == 0, "unmapped network address");
	if (bounds) {
		memset(overlong, '1', sizeof overlong);
		overlong[sizeof overlong - 1] = 0;
		memset(unterminated, '1', sizeof unterminated);
		check(ipv4_addr(base, overlong) == 0xffffffffUL, "overlong address");
		check(ipv4_network(base, overlong) == 0xffffffffUL, "overlong network");
		check(ipv4_addr(base, unterminated) == 0xffffffffUL, "missing NUL");
		check(ipv4_network(base, unterminated) == 0xffffffffUL, "network missing NUL");
		check(socket_errno(base) == before, "bounds failure preserves errno");
	}
	if (port)
		echo_test(base, port);
	CloseLibrary(base);
	Printf("%s: %ld checks, %ld failures\n", failures ? "FAIL" : "PASS",
		checks, failures);
	return failures ? 20 : 0;
}
