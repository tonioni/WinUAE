#ifndef UAE_HARDFILE_CACHE_H
#define UAE_HARDFILE_CACHE_H

#include "uae/types.h"

#define HDF_HOST_CACHE_SIZE 16384
struct hardfiledata;

typedef int (*hdf_host_reader)(struct hardfiledata *hfd, void *buffer,
	uae_u64 offset, int length, uae_u32 *error);

bool hdf_check_bounds(const struct hardfiledata *hfd, uae_u64 offset, int length);
int hdf_read_cached(struct hardfiledata *hfd, void *buffer, uae_u64 offset,
	int length, uae_u32 *error, hdf_host_reader read);
bool hdf_compressed_name(const TCHAR *name);

#endif
