
#include "sysconfig.h"
#include "sysdeps.h"

static unsigned long crc_table[256];
static void make_crc_table()
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++)	
    {
	c = (unsigned long)n;
	for (k = 0; k < 8; k++) c = (c >> 1) ^ (c & 1 ? 0xedb88320 : 0);
	    crc_table[n] = c;
    }
}
uae_u32 get_crc32 (uae_u8 *buf, int len)
{
    uae_u32 crc;
    if (!crc_table[1])
	make_crc_table();
    crc = 0xffffffff;
    while (len-- > 0) {
	crc = crc_table[(crc ^ (*buf++)) & 0xff] ^ (crc >> 8);    
    }
    return crc ^ 0xffffffff;
}
