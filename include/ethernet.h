#ifndef UAE_ETHERNET_H
#define UAE_ETHERNET_H

#include "uae/types.h"

#define UAENET_NONE 0
#define UAENET_SLIRP 1
#define UAENET_SLIRP_INBOUND 2
#define UAENET_PCAP 3

struct netdriverdata
{
	int type;
	const TCHAR *name;
	const TCHAR *desc;
	int mtu;
	uae_u8 mac[6];
	int active;
};


typedef void (ethernet_gotfunc)(void *dev, const uae_u8 *data, int len);
typedef int (ethernet_getfunc)(void *dev, uae_u8 *d, int *len);

extern bool ethernet_enumerate (struct netdriverdata **, const TCHAR *name);
extern void ethernet_enumerate_free (void);
extern void ethernet_close_driver (struct netdriverdata *ndd);

extern int ethernet_getdatalenght (struct netdriverdata *ndd);
extern int ethernet_open (struct netdriverdata *ndd, void*, void*, ethernet_gotfunc*, ethernet_getfunc*, int);
extern void ethernet_close (struct netdriverdata *ndd, void*);
extern void ethernet_trigger (struct netdriverdata *ndd, void*);

extern bool ariadne2_init(struct autoconfig_info *aci);
extern bool xsurf_init(struct autoconfig_info *aci);
extern bool xsurf100_init(struct autoconfig_info *aci);

void rethink_ne2000(void);
void ne2000_reset(void);
void ne2000_hsync(void);

#endif /* UAE_ETHERNET_H */
