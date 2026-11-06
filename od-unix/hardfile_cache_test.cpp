#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "filesys.h"
#include "hardfile_cache.h"

static uae_u8 image[65536];
static int reads;
static bool short_read;
static uae_u64 last_offset;

static void check(bool ok)
{
	if (!ok) {
		fprintf(stderr, "hardfile cache regression\n");
		exit(1);
	}
}

static int read_host(hardfiledata *hfd, void *buffer, uae_u64 offset,
	int length, uae_u32 *)
{
	check(offset <= hfd->physsize && (uae_u64)length <= hfd->physsize - offset);
	check(offset + hfd->offset + length <= sizeof image);
	reads++;
	last_offset = offset;
	memcpy(buffer, image + hfd->offset + offset, length);
	return short_read ? length - 1 : length;
}

int main(void)
{
	hardfiledata hfd = {};
	uae_u8 cache[HDF_HOST_CACHE_SIZE], output[32768];
	for (unsigned int i = 0; i < sizeof image; i++) {
		image[i] = (uae_u8)(i * 17 + i / 512);
	}
	hfd.cache = cache;
	hfd.offset = 4096;
	hfd.physsize = 32768;
	hfd.ci.blocksize = 512;
	uae_u32 error;
	check(hdf_read_cached(&hfd, output, 512, 512, &error, read_host) == 512);
	check(!error && reads == 1 && hfd.cache_valid);
	check(!memcmp(output, image + 4096 + 512, 512));
	check(hdf_read_cached(&hfd, output, 1024, 512, &error, read_host) == 512);
	check(reads == 1 && !memcmp(output, image + 4096 + 1024, 512));
	check(hdf_read_cached(&hfd, output, 32768 - 512, 512, &error, read_host) == 512);
	check(!error && reads == 2 && last_offset == 16384);
	check(!memcmp(output, image + 4096 + 32768 - 512, 512));
	check(hdf_read_cached(&hfd, output, 0, 32768, &error, read_host) == 32768);
	check(!memcmp(output, image + 4096, 32768));
	int old_reads = reads;
	check(!hdf_read_cached(&hfd, output, 32768, 512, &error, read_host) && error);
	check(!hdf_read_cached(&hfd, output, 32768 - 512, 1024, &error, read_host) && error);
	check(!hdf_read_cached(&hfd, output, 0, -1, &error, read_host) && error);
	check(!hdf_read_cached(&hfd, output, 1, 512, &error, read_host) && error);
	check(!hdf_read_cached(&hfd, output, ~(uae_u64)0, 512, &error, read_host) && error);
	check(reads == old_reads);
	check(!hdf_read_cached(&hfd, NULL, 32768, 0, &error, read_host) && !error);
	hfd.physsize = ~(uae_u64)0;
	check(!hdf_check_bounds(&hfd, 0x8000000000000000ULL, 512));
	hfd.physsize = 32768;
	hfd.offset = 0x7ffffffffffffe00ULL;
	check(!hdf_check_bounds(&hfd, 512, 512));
	hfd.offset = 4096;
	hfd.cache_valid = 0;
	short_read = true;
	check(!hdf_read_cached(&hfd, output, 512, 512, &error, read_host));
	check(error && !hfd.cache_valid);
	short_read = false;
	hfd.physsize = 1024;
	check(hdf_read_cached(&hfd, output, 512, 512, &error, read_host) == 512);
	check(!error && !memcmp(output, image + 4096 + 512, 512));
	/* A read of sector zero refreshes the cached view after media changes. */
	image[4096] ^= 0xff;
	check(hdf_read_cached(&hfd, output, 0, 512, NULL, read_host) == 512);
	check(output[0] == image[4096]);
	check(hdf_compressed_name(_T("image.HDZ")));
	check(hdf_compressed_name(_T("dir/image.zip")));
	check(!hdf_compressed_name(_T("dir.zip/image.hdf")));
	puts("Hardfile cache tests passed");
	return 0;
}
