#include "sysconfig.h"
#include "sysdeps.h"
#include "uaenet_common.h"
#include "threaddep/thread.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

// Exercise the native thread/semaphore implementation, with controlled
// thread-creation failures at the two worker startup points.
#define uae_start_thread uaenet_test_start_real
#include "../od-unix/threading.cpp"
#undef uae_start_thread

static int start_calls, fail_start;
int uae_start_thread(const TCHAR *name, uae_thread_function fn, void *arg, uae_thread_id *thread)
{
	if (++start_calls == fail_start) return 0;
	return uaenet_test_start_real(name, fn, arg, thread);
}
void write_log(const TCHAR *, ...) {}
extern int log_ethernet;
static int failures;
static void check(bool ok, const char *name)
{
	if (!ok) { fprintf(stderr, "FAIL: %s\n", name); failures++; }
}

struct test_backend {
	std::mutex mutex;
	std::condition_variable changed;
	std::deque<std::vector<uae_u8>> incoming, outgoing;
	std::vector<std::vector<uae_u8>> received, sent;
	bool interrupted = false;
	bool read_error = false;
	int reads = 0;
	std::atomic<int> callbacks{0};
	std::atomic<int> overlaps{0};
};
static int receive(void *user, uae_u8 *buffer, int capacity, const uae_u8 **packet, int *length)
{
	test_backend *b = (test_backend*)user;
	std::unique_lock<std::mutex> lock(b->mutex);
	b->changed.wait_for(lock, std::chrono::milliseconds(20), [&] {
		return b->interrupted || b->read_error || !b->incoming.empty();
	});
	if (b->interrupted || b->read_error) { b->reads++; b->changed.notify_all(); return -1; }
	if (b->incoming.empty()) return 0;
	std::vector<uae_u8> data = b->incoming.front(); b->incoming.pop_front();
	if (data.size() > (size_t)capacity) return -1;
	memcpy(buffer, data.data(), data.size());
	*packet = buffer; *length = (int)data.size();
	b->reads++; b->changed.notify_all();
	return 1;
}
static void send(void *user, const uae_u8 *packet, int length)
{
	test_backend *b = (test_backend*)user;
	std::lock_guard<std::mutex> lock(b->mutex);
	b->sent.emplace_back(packet, packet + length); b->changed.notify_all();
}
static void interrupt(void *user)
{
	test_backend *b = (test_backend*)user;
	std::lock_guard<std::mutex> lock(b->mutex);
	b->interrupted = true; b->changed.notify_all();
}
static int get(void *user, uae_u8 *packet, int *length)
{
	test_backend *b = (test_backend*)user;
	if (b->callbacks.fetch_add(1)) b->overlaps++;
	std::this_thread::yield();
	std::lock_guard<std::mutex> lock(b->mutex);
	int result = 0;
	if (!b->outgoing.empty()) {
		const auto &data = b->outgoing.front();
		check(data.size() <= (size_t)*length, "writer capacity");
		*length = (int)data.size(); memcpy(packet, data.data(), data.size());
		b->outgoing.pop_front(); result = 1;
	}
	b->callbacks--;
	return result;
}
static void got(void *user, const uae_u8 *packet, int length)
{
	test_backend *b = (test_backend*)user;
	if (b->callbacks.fetch_add(1)) b->overlaps++;
	std::this_thread::yield();
	std::lock_guard<std::mutex> lock(b->mutex);
	b->received.emplace_back(packet, packet + length);
	b->callbacks--; b->changed.notify_all();
}
static const uaenet_backend backend = { receive, send, interrupt };
static bool wait_count(test_backend &b, int received, int sent, int reads)
{
	std::unique_lock<std::mutex> lock(b.mutex);
	return b.changed.wait_for(lock, std::chrono::seconds(3), [&] {
		return b.received.size() >= (size_t)received && b.sent.size() >= (size_t)sent && b.reads >= reads;
	});
}
static void workers()
{
	for (int timeout : { -1, 100 }) {
		test_backend b;
		std::vector<uae_u8> packet(1600);
		for (int i = 0; i < 1600; i++) packet[i] = (uae_u8)i;
		uaenet_worker *w = uaenet_start_workers(&backend, &b, &b, got, get, 2000, timeout);
		check(w != NULL, "start workers"); if (!w) continue;
		log_ethernet = 1;
		{
			std::lock_guard<std::mutex> lock(b.mutex);
			for (int i = 0; i < 64; i++) { b.incoming.push_back(packet); b.outgoing.push_back(packet); }
			b.changed.notify_all();
		}
		uaenet_wake_writer(w);
		check(wait_count(b, 64, 64, 64), "drain packets after one wakeup");
		ethernet_pause(1);
		{
			std::lock_guard<std::mutex> lock(b.mutex);
			b.incoming.push_back(packet); b.changed.notify_all();
		}
		check(wait_count(b, 64, 64, 65), "read while paused");
		// Let the paused delivery decision complete before resuming.
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		ethernet_reset();
		{
			std::lock_guard<std::mutex> lock(b.mutex);
			b.incoming.push_back(packet); b.changed.notify_all();
		}
		check(wait_count(b, 65, 64, 66), "resume receive");
		uaenet_stop_workers(w);
		check(b.received.size() == 65 && b.sent.size() == 64, "pause discards one packet");
		check(b.overlaps == 0, "serialize guest callbacks");
		for (const auto &data : b.received) check(data == packet, "receive payload");
		for (const auto &data : b.sent) check(data == packet, "send payload at logging boundary");
		log_ethernet = 0;
	}
	for (int fail : { 1, 2 }) {
		test_backend b; start_calls = 0; fail_start = fail;
		check(uaenet_start_workers(&backend, &b, &b, got, get, 1522, 100) == NULL, "thread startup failure cleanup");
	}
	fail_start = 0;
	for (int i = 0; i < 20; i++) {
		test_backend b; b.read_error = true;
		uaenet_worker *w = uaenet_start_workers(&backend, &b, &b, got, get, 1522, -1);
		check(w != NULL && wait_count(b, 0, 0, 1), "reader exits on backend error");
		uaenet_stop_workers(w);
	}
	uaenet_stop_workers(NULL); uaenet_wake_writer(NULL);
}
static void drivers()
{
	netdriverdata list[3] = {};
	list[1].name = _T("Adapter One"); list[1].active = 1;
	memcpy(list[1].mac, uaenet_mac, 6); list[1].mac[5] = 1;
	memcpy(list[1].originalmac, list[1].mac, 6);
	check(uaenet_find_driver(list, 3, NULL) == list, "enumerate array");
	check(uaenet_find_driver(list, 3, _T("adapter ONE")) == list + 1, "case insensitive name");
	check(uaenet_find_driver(list, 3, _T("aa:82:8a:00:00:01")) == list + 1, "MAC lookup");
	check(!uaenet_find_driver(list, 3, _T("missing")), "unknown adapter");
	check(uaenet_select_mac(list + 1, NULL, 0) == 0, "original MAC");
	check(uaenet_select_mac(list + 1, uaenet_mac, 0) == 1, "override requires promiscuous capture");
	check(uaenet_select_mac(list + 1, list[1].originalmac, 1) == 1, "explicit promiscuous mode");
	uaenet_clear_drivers(list, 3, false);
	check(!list[1].active && list[1].name, "deactivate without releasing names");
	check(!uaenet_find_driver(list, 3, _T("Adapter One")), "inactive adapter");
	list[1].name = strdup("owned name"); list[1].desc = strdup("owned description");
	uaenet_clear_drivers(list, 3, true);
	check(!list[1].name && !list[1].desc, "release owned names");
}
int main()
{
	drivers(); workers();
	if (!failures) puts("Network worker, lifecycle, pause and device lookup tests passed");
	return failures ? 1 : 0;
}
