#include "sysconfig.h"
#include "sysdeps.h"
#include "bsdsocket.h"
#include "bsdsocket_results.h"
#include "options.h"
#include "memory.h"
#include "autoconf.h"
#include "native2amiga.h"
#include "traps.h"
#include <map>
#include <string>
#include <vector>

static uae_u8 guest[65536];
static std::map<uaecptr, uae_u32> allocations;
static uaecptr next_address = 128;
static bool fail_allocation;

static void check(bool ok)
{
	if (!ok) {
		fprintf(stderr, "socket result regression\n");
		exit(1);
	}
}

uaecptr uae_AllocMem(TrapContext *, uae_u32 size, uae_u32, uaecptr)
{
	if (fail_allocation) {
		return 0;
	}
	uaecptr address = next_address;
	check(size < sizeof guest - address - 16);
	next_address += size + 16;
	allocations[address] = size;
	memset(guest + address, 0xcc, size + 16);
	return address;
}

void uae_FreeMem(TrapContext *, uaecptr address, uae_u32 size, uaecptr)
{
	check(allocations.count(address) && allocations[address] == size);
	for (unsigned int i = 0; i < 16; i++) {
		check(guest[address + size + i] == 0xcc);
	}
	allocations.erase(address);
}

void bsdsocklib_seterrno(TrapContext *, socketbase *sb, int error)
{
	sb->sb_errno = error;
}

void bsdsocklib_setherrno(TrapContext *, socketbase *sb, int error)
{
	sb->sb_herrno = error;
}

void trap_put_bytes(TrapContext *, const void *data, uaecptr address, int size)
{
	bool valid = false;
	for (const auto &allocation : allocations) {
		if (address >= allocation.first && size >= 0
			&& (uae_u64)address + size <= allocation.first + allocation.second) {
			valid = true;
		}
	}
	check(valid);
	memcpy(guest + address, data, size);
}

void trap_put_long(TrapContext *ctx, uaecptr address, uae_u32 value)
{
	uae_u8 bytes[] = { (uae_u8)(value >> 24), (uae_u8)(value >> 16),
		(uae_u8)(value >> 8), (uae_u8)value };
	trap_put_bytes(ctx, bytes, address, 4);
}

static uae_u32 read_long(uaecptr address)
{
	return ((uae_u32)guest[address] << 24) | ((uae_u32)guest[address + 1] << 16)
		| (guest[address + 2] << 8) | guest[address + 3];
}

static bool string_at(uaecptr address, const char *value)
{
	return !strcmp((char*)guest + address, value);
}

int main(void)
{
	socketbase sb = {};
	const char *aliases[] = { "alias", "second", NULL };
	const char address1[] = { 127, 0, 0, 1 }, address2[] = { 10, 1, 2, 3 };
	const char *addresses[] = { address1, address2, NULL };
	check(bsdsock_copy_hostent(NULL, &sb, "localhost", aliases, 2, 4, addresses));
	check(read_long(sb.hostent + 8) == 2 && read_long(sb.hostent + 12) == 4);
	check(string_at(read_long(sb.hostent), "localhost"));
	uaecptr table = read_long(sb.hostent + 4);
	check(string_at(read_long(table), "alias"));
	check(string_at(read_long(table + 4), "second") && !read_long(table + 8));
	table = read_long(sb.hostent + 16);
	check(!memcmp(guest + read_long(table), address1, 4));
	check(!memcmp(guest + read_long(table + 4), address2, 4) && !read_long(table + 8));
	uaecptr old = sb.hostent;
	check(bsdsock_copy_hostent(NULL, &sb, "x", NULL, 2, 4, NULL));
	check(sb.hostent == old);
	check(!read_long(read_long(sb.hostent + 4)) && !read_long(read_long(sb.hostent + 16)));
	std::string large(4096, 'x');
	check(bsdsock_copy_hostent(NULL, &sb, large.c_str(), aliases, 2, 4, addresses));
	check(sb.hostentsize > 4096 && !allocations.count(old));
	check(string_at(read_long(sb.hostent), large.c_str()));
	check(bsdsock_copy_protoent(NULL, &sb, "tcp", aliases, 6));
	check(string_at(read_long(sb.protoent), "tcp") && read_long(sb.protoent + 8) == 6);
	check(bsdsock_copy_servent(NULL, &sb, "http", aliases, 80, "tcp"));
	check(string_at(read_long(sb.servent), "http") && read_long(sb.servent + 8) == 80);
	check(string_at(read_long(sb.servent + 12), "tcp"));
	check(bsdsock_copy_servent(NULL, &sb, NULL, NULL, 0, NULL));
	check(!read_long(sb.servent) && !read_long(sb.servent + 12));
	check(!bsdsock_copy_hostent(NULL, &sb, "x", NULL, 2, -1, addresses));
	check(sb.sb_errno == 22 && sb.sb_herrno != 0);
	/* An oversized result must not read addresses or perform a guest write. */
	check(!bsdsock_copy_hostent(NULL, &sb, "x", NULL, 2, 0x7fffffff, addresses));
	check(sb.sb_errno == 12 && sb.sb_herrno != 0);
	fail_allocation = true;
	large.resize(8192, 'y');
	check(!bsdsock_copy_hostent(NULL, &sb, large.c_str(), aliases, 2, 4, addresses));
	check(!sb.hostent && !sb.hostentsize && sb.sb_errno == 12);
	check(!bsdsock_copy_protoent(NULL, &sb, large.c_str(), aliases, 6));
	check(!sb.protoent && !sb.protoentsize);
	check(!bsdsock_copy_servent(NULL, &sb, large.c_str(), aliases, 80, "tcp"));
	check(!sb.servent && !sb.serventsize && allocations.empty());
	fail_allocation = false;
	check(bsdsock_copy_hostent(NULL, &sb, NULL, NULL, 2, 4, NULL));
	check(!read_long(sb.hostent) && !sb.sb_errno && !sb.sb_herrno);
	uae_FreeMem(NULL, sb.hostent, sb.hostentsize, 0);
	check(allocations.empty());
	puts("Socket result tests passed");
	return 0;
}
