
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

#define SLIRP_PORT_OFFSET 0

static const int slirp_ports[] = { 21, 22, 23, 80, 0 };

static struct ethernet_data *slirp_data;
static bool slirp_inited;
uae_sem_t slirp_sem1, slirp_sem2;
static int netmode;

static struct netdriverdata slirpd =
{
	UAENET_SLIRP,
	_T("slirp"), _T("SLIRP User Mode NAT"),
	1500,
	{ 0x00,0x80,0x10,50,51,52 },
	1
};
static struct netdriverdata slirpd2 =
{
	UAENET_SLIRP_INBOUND,
	_T("slirp_inbound"), _T("SLIRP + Open ports (21-23,80)"),
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
		case UAENET_SLIRP_INBOUND:
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
		case UAENET_SLIRP_INBOUND:
		{
			struct ethernet_data *ed = (struct ethernet_data*)vsd;
			ed->gotfunc = gotfunc;
			ed->getfunc = getfunc;
			slirp_data = ed;
			uae_sem_init (&slirp_sem1, 0, 1);
			uae_sem_init (&slirp_sem2, 0, 1);
			slirp_init ();
			for (int i = 0; i < MAX_SLIRP_REDIRS; i++) {
				struct slirp_redir *sr = &currprefs.slirp_redirs[i];
				if (sr->proto) {
					struct in_addr a;
					if (sr->srcport == 0) {
					    inet_aton("10.0.2.15", &a);
						slirp_redir (0, sr->dstport, a, sr->dstport);
					} else {
						a.S_un.S_addr = sr->addr;
						slirp_redir (sr->proto == 1 ? 0 : 1, sr->dstport, a, sr->srcport);
					}
				}
			}
			if (ndd->type == UAENET_SLIRP_INBOUND) {
				struct in_addr a;
			    inet_aton("10.0.2.15", &a);
				for (int i = 0; slirp_ports[i]; i++) {
					int port = slirp_ports[i];
					int j;
					for (j = 0; j < MAX_SLIRP_REDIRS; j++) {
						struct slirp_redir *sr = &currprefs.slirp_redirs[j];
						if (sr->proto && sr->dstport == port)
							break;
					}
					if (j == MAX_SLIRP_REDIRS)
						slirp_redir (0, port + SLIRP_PORT_OFFSET, a, port);
				}
			}
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
		case UAENET_SLIRP_INBOUND:
		if (slirp_data) {
			slirp_data = NULL;
			slirp_end ();
			slirp_cleanup ();
			uae_sem_destroy (&slirp_sem1);
			uae_sem_destroy (&slirp_sem2);
		}
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
		if (!_tcsicmp (slirpd2.name, name))
			*nddp = &slirpd2;
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
	nddp[j++] = &slirpd2;
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
		case UAENET_SLIRP_INBOUND:
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
		case UAENET_SLIRP_INBOUND:
		return sizeof (struct ethernet_data);
		case UAENET_PCAP:
		return uaenet_getdatalenght ();
	}
	return 0;
}