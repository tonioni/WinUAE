/* Shared host-image read cache, based on hardfile_win32.cpp. */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "filesys.h"
#include "hardfile_cache.h"

bool hdf_check_bounds(const struct hardfiledata *hfd, uae_u64 offset, int length)
{
	if (length < 0 || offset > hfd->physsize
		|| (uae_u64)length > hfd->physsize - offset) {
		return false;
	}
	if (offset > 0x7fffffffffffffffULL
		|| hfd->offset > 0x7fffffffffffffffULL - offset
		|| (uae_u64)length > 0x7fffffffffffffffULL - offset - hfd->offset) {
		return false;
	}
	return hfd->ci.blocksize > 0
		&& (offset + hfd->offset) % hfd->ci.blocksize == 0;
}

int hdf_read_cached(struct hardfiledata *hfd, void *buffer, uae_u64 offset,
	int length, uae_u32 *error, hdf_host_reader read)
{
	uae_u32 local_error = 0;
	if (!error) {
		error = &local_error;
	}
	*error = 0;
	if (!hdf_check_bounds(hfd, offset, length)) {
		*error = 45;
		return 0;
	}
	if (!length) {
		return 0;
	}
	if (!hfd->cache) {
		*error = 45;
		return 0;
	}
	/* Re-read the first block, as the Windows backend did for media checks. */
	if (!offset) {
		hfd->cache_valid = 0;
	}
	int cache_length = hfd->physsize < HDF_HOST_CACHE_SIZE
		? (int)hfd->physsize : HDF_HOST_CACHE_SIZE;
	uae_u8 *destination = (uae_u8*)buffer;
	int done = 0;
	while (length > 0) {
		if (!hfd->cache_valid || offset < hfd->cache_offset
			|| offset - hfd->cache_offset >= (uae_u64)cache_length) {
			hfd->cache_valid = 0;
			hfd->cache_offset = offset;
			if (hfd->physsize - offset < (uae_u64)cache_length) {
				hfd->cache_offset = hfd->physsize - cache_length;
			}
			int got = read(hfd, hfd->cache, hfd->cache_offset, cache_length, error);
			if (got != cache_length) {
				if (!*error) {
					*error = 45;
				}
				return done;
			}
			hfd->cache_valid = 1;
		}
		int position = (int)(offset - hfd->cache_offset);
		int count = cache_length - position;
		if (count > length) {
			count = length;
		}
		memcpy(destination, hfd->cache + position, count);
		destination += count;
		offset += count;
		length -= count;
		done += count;
	}
	return done;
}

bool hdf_compressed_name(const TCHAR *name)
{
	const TCHAR *extension = _tcsrchr(name, '.');
	if (!extension) {
		return false;
	}
	return !_tcsicmp(extension, _T(".hdz")) || !_tcsicmp(extension, _T(".zip"))
		|| !_tcsicmp(extension, _T(".rar")) || !_tcsicmp(extension, _T(".7z"));
}
