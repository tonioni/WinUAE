/* Shared conversion of host resolver results to the Amiga socket ABI. */
#include "sysconfig.h"
#include "sysdeps.h"

#ifdef BSDSOCKET
#include "bsdsocket.h"
#include "bsdsocket_results.h"
#include "options.h"
#include "memory.h"
#include "autoconf.h"
#include "native2amiga.h"
#include "traps.h"

static uae_u64 string_size(const char *s)
{
	return s ? (uae_u64)strlen(s) + 1 : 0;
}

static uae_u64 alias_size(const char *const *aliases, int *count)
{
	uae_u64 size = 0;
	*count = 0;
	while (aliases && aliases[*count]) {
		size += string_size(aliases[(*count)++]) + 4;
	}
	return size;
}

static bool result_buffer(TrapContext *ctx, struct socketbase *sb,
	uaecptr *address, uae_u32 *allocated, uae_u64 size)
{
	/* Guest allocation and trap byte counts must both represent the size. */
	if (size > 0x7fffffff) {
		bsdsocklib_seterrno(ctx, sb, 12);
		return false;
	}
	if (*address && *allocated < size) {
		uae_FreeMem(ctx, *address, *allocated, sb->sysbase);
		*address = 0;
		*allocated = 0;
	}
	if (!*address) {
		*allocated = 0;
		*address = uae_AllocMem(ctx, (uae_u32)size, 0, sb->sysbase);
		if (!*address) {
			bsdsocklib_seterrno(ctx, sb, 12);
			return false;
		}
		*allocated = (uae_u32)size;
	}
	return true;
}

static uaecptr put_string(TrapContext *ctx, uaecptr *cursor, const char *s)
{
	if (!s) {
		return 0;
	}
	uaecptr address = *cursor;
	int size = (int)strlen(s) + 1;
	trap_put_bytes(ctx, s, address, size);
	*cursor += size;
	return address;
}

static void put_aliases(TrapContext *ctx, uaecptr table, uaecptr *cursor,
	const char *const *aliases, int count)
{
	for (int i = 0; i < count; i++) {
		trap_put_long(ctx, table + i * 4, put_string(ctx, cursor, aliases[i]));
	}
	trap_put_long(ctx, table + count * 4, 0);
}

bool bsdsock_copy_hostent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int address_type,
	int address_length, const char *const *addresses)
{
	if (address_length < 0) {
		bsdsocklib_seterrno(ctx, sb, 22);
		bsdsocklib_setherrno(ctx, sb, 3);
		return false;
	}
	int aliases_count, addresses_count = 0;
	uae_u64 size = 28 + string_size(name) + alias_size(aliases, &aliases_count);
	while (addresses && addresses[addresses_count]) {
		addresses_count++;
	}
	size += (uae_u64)addresses_count * ((uae_u64)address_length + 4);
	if (!result_buffer(ctx, sb, &sb->hostent, &sb->hostentsize, size)) {
		bsdsocklib_setherrno(ctx, sb, 3);
		return false;
	}
	uaecptr base = sb->hostent;
	uaecptr address_table = base + 24 + aliases_count * 4;
	uaecptr cursor = address_table + (addresses_count + 1) * 4;
	trap_put_long(ctx, base + 4, base + 20);
	trap_put_long(ctx, base + 8, address_type);
	trap_put_long(ctx, base + 12, address_length);
	trap_put_long(ctx, base + 16, address_table);
	put_aliases(ctx, base + 20, &cursor, aliases, aliases_count);
	for (int i = 0; i < addresses_count; i++) {
		trap_put_long(ctx, address_table + i * 4, cursor);
		trap_put_bytes(ctx, addresses[i], cursor, address_length);
		cursor += address_length;
	}
	trap_put_long(ctx, address_table + addresses_count * 4, 0);
	trap_put_long(ctx, base, put_string(ctx, &cursor, name));
	bsdsocklib_seterrno(ctx, sb, 0);
	bsdsocklib_setherrno(ctx, sb, 0);
	return true;
}

bool bsdsock_copy_protoent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int protocol)
{
	int count;
	uae_u64 size = 16 + string_size(name) + alias_size(aliases, &count);
	if (!result_buffer(ctx, sb, &sb->protoent, &sb->protoentsize, size)) {
		return false;
	}
	uaecptr base = sb->protoent;
	uaecptr cursor = base + 16 + count * 4;
	trap_put_long(ctx, base + 4, base + 12);
	trap_put_long(ctx, base + 8, protocol);
	put_aliases(ctx, base + 12, &cursor, aliases, count);
	trap_put_long(ctx, base, put_string(ctx, &cursor, name));
	bsdsocklib_seterrno(ctx, sb, 0);
	return true;
}

bool bsdsock_copy_servent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int port, const char *protocol)
{
	int count;
	uae_u64 size = 20 + string_size(name) + string_size(protocol)
		+ alias_size(aliases, &count);
	if (!result_buffer(ctx, sb, &sb->servent, &sb->serventsize, size)) {
		return false;
	}
	uaecptr base = sb->servent;
	uaecptr cursor = base + 20 + count * 4;
	trap_put_long(ctx, base + 4, base + 16);
	trap_put_long(ctx, base + 8, port);
	put_aliases(ctx, base + 16, &cursor, aliases, count);
	trap_put_long(ctx, base, put_string(ctx, &cursor, name));
	trap_put_long(ctx, base + 12, put_string(ctx, &cursor, protocol));
	bsdsocklib_seterrno(ctx, sb, 0);
	return true;
}
#endif
