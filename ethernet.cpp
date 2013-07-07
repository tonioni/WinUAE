
#include "slirp/slirp.h"
#include "slirp/libslirp.h"

#ifdef _WIN32
#include "win32_uaenet.h"
#else
#include "ethernet.h"
#endif
#include "threaddep/thread.h"
#include "options.h"

struct ethernet_data
{
	ethernet_gotfunc *gotfunc;
	ethernet_getfunc *getfunc;
};


static int getmode (void)
{
	if (!_tcsicmp (currprefs.a2065name, _T("none")) || currprefs.a2065name[0] == 0)
		return UAENET_NONE;
	if (!_tcsicmp (currprefs.a2065name, _T("slirp")))
		return UAENET_SLIRP;
	return UAENET_PCAP;
}

static struct ethernet_data *slirp_data;
uae_sem_t slirp_sem1, slirp_sem2;
static int netmode;

static struct netdriverdata slirpd = {
	UAENET_SLIRP,
	_T("slirp"), _T("SLIRP User Mode NAT"),
	1500,
	{ 0x00,0x80,0x10,50,51,52 },
	1
};

int slirp_can_output(void)
{
	return 1;
}

void slirp_output (const uint8 *pkt, int pkt_len)
{
	if (!slirp_data)
		return;
	uae_sem_wait (&slirp_sem1);
	slirp_data->gotfunc (NULL, pkt, pkt_len);
	uae_sem_post (&slirp_sem1);
}

void ethernet_trigger (void *vsd)
{
	switch (netmode)
	{
		case UAENET_SLIRP:
		{
			struct ethernet_data *ed = (struct ethernet_data*)vsd;
			if (slirp_data) {
				uae_u8 pkt[4000];
				int len = sizeof pkt;
				int v;
				uae_sem_wait (&slirp_sem1);
				v = slirp_data->getfunc(NULL, pkt, &len);
				uae_sem_post (&slirp_sem1);
				if (v) {
					uae_sem_wait (&slirp_sem2);
					slirp_input(pkt, len);
					uae_sem_post (&slirp_sem2);
				}
			}
		}
		return;
		case UAENET_PCAP:
		uaenet_trigger (vsd);
		return;
	}
}

int ethernet_open (struct netdriverdata *ndd, void *vsd, void *user, ethernet_gotfunc *gotfunc, ethernet_getfunc *getfunc, int promiscuous)
{
	switch (ndd->type)
	{
		case UAENET_SLIRP:
		{
			struct ethernet_data *ed = (struct ethernet_data*)vsd;
			ed->gotfunc = gotfunc;
			ed->getfunc = getfunc;
			slirp_data = ed;
			uae_sem_init (&slirp_sem1, 0, 1);
			uae_sem_init (&slirp_sem2, 0, 1);
			slirp_init ();
			slirp_start ();
		}
		return 1;
		case UAENET_PCAP:
		return uaenet_open (vsd, ndd, user, gotfunc, getfunc, promiscuous);
	}
	return 0;
}

void ethernet_close (struct netdriverdata *ndd, void *vsd)
{
	if (!ndd)
		return;
	switch (ndd->type)
	{
		case UAENET_SLIRP:
		slirp_end ();
		slirp_data = NULL;
		return;
		case UAENET_PCAP:
		return uaenet_close (vsd);
	}
}

void ethernet_enumerate_free (void)
{
	uaenet_enumerate_free ();
}

bool ethernet_enumerate (struct netdriverdata **nddp, const TCHAR *name)
{
	int j;
	struct netdriverdata *nd;
	if (name) {
		netmode = 0;
		*nddp = NULL;
		if (!_tcsicmp (slirpd.name, name))
			*nddp = &slirpd;
		if (*nddp == NULL)
			*nddp = uaenet_enumerate (name);
		if (*nddp) {
			netmode = (*nddp)->type;
			return true;
		}
		return false;
	}
	j = 0;
	nddp[j++] = &slirpd;
	nd = uaenet_enumerate (NULL);
	if (nd) {
		for (int i = 0; i < nd[i].active; i++) {
			nddp[j++] = nd;
		}
	}
	nddp[j] = NULL;
	return true;
}

void ethernet_close_driver (struct netdriverdata *ndd)
{
	switch (ndd->type)
	{
		case UAENET_SLIRP:
		return;
		case UAENET_PCAP:
		return uaenet_close_driver (ndd);
	}
	netmode = 0;
}

int ethernet_getdatalenght (struct netdriverdata *ndd)
{
	switch (ndd->type)
	{
		case UAENET_SLIRP:
		return sizeof (struct ethernet_data);
		case UAENET_PCAP:
		return uaenet_getdatalenght ();
	}
	return 0;
}