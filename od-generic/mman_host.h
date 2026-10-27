#ifndef OD_GENERIC_MMAN_HOST_H
#define OD_GENERIC_MMAN_HOST_H

#include "uae/types.h"

/* Host functions used by od-generic/mman.cpp. */

/* Physical RAM, physical RAM plus available swap/page file, and whether the
 * host OS is 64-bit (also true for a 32-bit build running on a 64-bit OS). */
void host_mman_get_memory(uae_u64 *totalphys, uae_u64 *total, bool *os_64bit);

#endif /* OD_GENERIC_MMAN_HOST_H */
