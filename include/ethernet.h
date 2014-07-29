#ifndef _UAE_ETHERNET_H_
#define _UAE_ETHERNET_H_

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


typedef void (ethernet_gotfunc)(struct s2devstruct *dev, const uae_u8 *data, int len);
typedef int (ethernet_getfunc)(struct s2devstruct *dev, uae_u8 *d, int *len);

extern bool ethernet_enumerate (struct netdriverdata **, const TCHAR *name);
extern void ethernet_enumerate_free (void);
extern void ethernet_close_driver (struct netdriverdata *ndd);

extern int ethernet_getdatalenght (struct netdriverdata *ndd);
extern int ethernet_getbytespending (void*);
extern int ethernet_open (struct netdriverdata *ndd, void*, void*, ethernet_gotfunc*, ethernet_getfunc*, int);
extern void ethernet_close (struct netdriverdata *ndd, void*);
extern void ethernet_gotdata (struct s2devstruct *dev, const uae_u8 *data, int len);
extern int ethernet_getdata (struct s2devstruct *dev, uae_u8 *d, int *len);
extern void ethernet_trigger (void*);

extern bool slirp_start (void);
extern void slirp_end (void);

#endif // _UAE_ETHERNET_H_
