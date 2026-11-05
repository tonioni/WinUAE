#ifndef UAE_UAENET_COMMON_H
#define UAE_UAENET_COMMON_H

#include "uaenet.h"

struct uaenet_worker;

// Receive returns one packet, zero on timeout, or a negative error. A packet
// remains valid until the next receive call. Receive must return periodically
// or provide an interrupt callback that wakes it before the host device closes.
struct uaenet_backend {
	int (*receive)(void *user, uae_u8 *buffer, int capacity,
		const uae_u8 **packet, int *length);
	void (*send)(void *user, const uae_u8 *packet, int length);
	void (*interrupt)(void *user);
};

uaenet_worker *uaenet_start_workers(const uaenet_backend *backend,
	void *backend_user, void *user, uaenet_gotfunc *gotfunc,
	uaenet_getfunc *getfunc, int mtu, int write_timeout);
void uaenet_stop_workers(uaenet_worker *worker);
void uaenet_wake_writer(uaenet_worker *worker);
bool uaenet_is_paused(void);

extern const uae_u8 uaenet_mac[6];
int uaenet_select_mac(netdriverdata *driver, const uae_u8 *mac,
	int promiscuous);
netdriverdata *uaenet_find_driver(netdriverdata *drivers, int count,
	const TCHAR *name);
void uaenet_clear_drivers(netdriverdata *drivers, int count,
	bool release_names);

#endif
