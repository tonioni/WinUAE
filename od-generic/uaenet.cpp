/*
 * UAE - The Un*x Amiga Emulator
 *
 * Shared host packet workers and network device lookup.
 *
 * Copyright 2007 Toni Wilen
 */
#include "sysconfig.h"
#include "sysdeps.h"
#include "uaenet_common.h"
#include "threaddep/thread.h"
#include <atomic>
#include <new>

int log_ethernet;
static std::atomic<int> ethernet_paused(0);

// Locally administered unicast MAC: U<<1, A<<1, E<<1.
const uae_u8 uaenet_mac[6] = { 0xaa, 0x82, 0x8a, 0x00, 0x00, 0x00 };

struct uaenet_worker {
	const uaenet_backend *backend;
	void *backend_user;
	void *user;
	uaenet_gotfunc *gotfunc;
	uaenet_getfunc *getfunc;
	uae_u8 *readbuffer = NULL;
	uae_u8 *writebuffer = NULL;
	int mtu;
	int write_timeout;
	std::atomic<bool> running;
	bool read_started = false;
	bool write_started = false;
	uae_thread_id reader = 0;
	uae_thread_id writer = 0;
	uae_sem_t ready = NULL;
	uae_sem_t change = NULL;
	uae_sem_t wake = NULL;
};

static void receive_packets(void *arg)
{
	uaenet_worker *worker = (uaenet_worker*)arg;
	uae_set_thread_priority(NULL, 1);
	uae_sem_post(&worker->ready);
	while (worker->running.load()) {
		const uae_u8 *packet = NULL;
		int length = 0;
		int result = worker->backend->receive(worker->backend_user,
			worker->readbuffer, worker->mtu + 64, &packet, &length);
		if (result < 0) {
			break;
		}
		if (result > 0 && packet && length > 0 && !uaenet_is_paused()) {
			uae_sem_wait(&worker->change);
			worker->gotfunc(worker->user, packet, length);
			uae_sem_post(&worker->change);
		}
	}
}

static void send_packets(void *arg)
{
	uaenet_worker *worker = (uaenet_worker*)arg;
	uae_set_thread_priority(NULL, 1);
	uae_sem_post(&worker->ready);
	while (worker->running.load()) {
		int length = worker->mtu;
		uae_sem_wait(&worker->change);
		bool wrote = worker->getfunc(worker->user, worker->writebuffer, &length) != 0;
		if (wrote && length >= 0 && length <= worker->mtu + 64) {
			if (log_ethernet & 1) {
				TCHAR out[1600 * 2 + 1] = { 0 };
				for (int i = 0; i < length && i < 1600; i++) {
					_stprintf(out + i * 2, _T("%02x"), worker->writebuffer[i]);
				}
				write_log(_T("OUT %4d: %s\n"), length, out);
			}
			worker->backend->send(worker->backend_user, worker->writebuffer, length);
		}
		uae_sem_post(&worker->change);
		if (!wrote) {
			if (worker->write_timeout < 0) {
				uae_sem_wait(&worker->wake);
			} else {
				uae_sem_trywait_delay(&worker->wake, worker->write_timeout);
			}
		}
	}
}

uaenet_worker *uaenet_start_workers(const uaenet_backend *backend,
	void *backend_user, void *user, uaenet_gotfunc *gotfunc,
	uaenet_getfunc *getfunc, int mtu, int write_timeout)
{
	uaenet_worker *worker = new (std::nothrow) uaenet_worker;
	if (!worker) {
		return NULL;
	}
	worker->backend = backend;
	worker->backend_user = backend_user;
	worker->user = user;
	worker->gotfunc = gotfunc;
	worker->getfunc = getfunc;
	worker->mtu = mtu > 0 ? mtu : 1522;
	worker->write_timeout = write_timeout;
	worker->running.store(true);
	worker->readbuffer = xmalloc(uae_u8, worker->mtu + 64);
	worker->writebuffer = xmalloc(uae_u8, worker->mtu + 64);
	if (!worker->readbuffer || !worker->writebuffer) {
		uaenet_stop_workers(worker);
		return NULL;
	}
	uae_sem_init(&worker->ready, 0, 0);
	uae_sem_init(&worker->change, 0, 1);
	uae_sem_init(&worker->wake, 0, 0);
	if (!uae_start_thread(_T("uaenet_read"), receive_packets, worker, &worker->reader)) {
		uaenet_stop_workers(worker);
		return NULL;
	}
	worker->read_started = true;
	uae_sem_wait(&worker->ready);
	if (!uae_start_thread(_T("uaenet_write"), send_packets, worker, &worker->writer)) {
		uaenet_stop_workers(worker);
		return NULL;
	}
	worker->write_started = true;
	uae_sem_wait(&worker->ready);
	return worker;
}

void uaenet_wake_writer(uaenet_worker *worker)
{
	if (worker) {
		uae_sem_post(&worker->wake);
	}
}

void uaenet_stop_workers(uaenet_worker *worker)
{
	if (!worker) {
		return;
	}
	worker->running.store(false);
	if (worker->read_started && worker->backend->interrupt) {
		worker->backend->interrupt(worker->backend_user);
	}
	uaenet_wake_writer(worker);
	// Join even a reader that has already exited after a backend error.
	if (worker->read_started) {
		uae_wait_thread(worker->reader);
	}
	if (worker->write_started) {
		uae_wait_thread(worker->writer);
	}
	uae_sem_destroy(&worker->ready);
	uae_sem_destroy(&worker->change);
	uae_sem_destroy(&worker->wake);
	xfree(worker->readbuffer);
	xfree(worker->writebuffer);
	delete worker;
}

bool uaenet_is_paused(void)
{
	return ethernet_paused.load() != 0;
}

void ethernet_pause(int pause)
{
	ethernet_paused.store(pause);
}

void ethernet_reset(void)
{
	ethernet_paused.store(0);
}

int uaenet_select_mac(netdriverdata *driver, const uae_u8 *mac, int promiscuous)
{
	if (mac) {
		memcpy(driver->mac, mac, 6);
	}
	return memcmp(driver->mac, driver->originalmac, 6) ? 1 : promiscuous;
}

netdriverdata *uaenet_find_driver(netdriverdata *drivers, int count, const TCHAR *name)
{
	if (!name) {
		return drivers;
	}
	for (int i = 0; i < count; i++) {
		netdriverdata *driver = &drivers[i];
		if (!driver->active || !driver->name) {
			continue;
		}
		TCHAR mac[20];
		_stprintf(mac, _T("%02X:%02X:%02X:%02X:%02X:%02X"),
			driver->mac[0], driver->mac[1], driver->mac[2],
			driver->mac[3], driver->mac[4], driver->mac[5]);
		if (!_tcsicmp(name, driver->name) || !_tcsicmp(name, mac)) {
			return driver;
		}
	}
	return NULL;
}

void uaenet_clear_drivers(netdriverdata *drivers, int count, bool release_names)
{
	for (int i = 0; i < count; i++) {
		drivers[i].active = 0;
		if (release_names) {
			xfree((void*)drivers[i].name);
			xfree((void*)drivers[i].desc);
			drivers[i].name = NULL;
			drivers[i].desc = NULL;
		}
	}
}
