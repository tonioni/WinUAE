/* WinUAE loopback GDB server. No worker thread accesses emulated state. */
#include "sysconfig.h"
#include "sysdeps.h"
#include "options.h"
#include "gdb_server.h"
#include "gdb_protocol.h"
#include "gdb_transport.h"
#include "uae.h"
#include "xwin.h"
#include "sounddep/sound.h"
#include <memory>

#if defined(DEBUGGER) && defined(GDBSERVER)
namespace transport = winuae_gdb_transport;
namespace {
using transport::Socket;
using transport::invalid_socket;
using transport::close_socket;
Socket listener = invalid_socket, client = invalid_socket;
std::unique_ptr<winuae_gdb::Session> session, retired_session;
std::string pending;
bool polling, sockets_started, restoring;
int configured_port;
bool threaded_cpu_warned;

void disconnect_client()
{
	if (client != invalid_socket) close_socket(client);
	client = invalid_socket;
	if (session) debug_gdb_target().detach();
	if (polling && session) {
		session->abandon();
		retired_session = std::move(session);
	} else {
		session.reset();
	}
	pending.clear();
}

void listen_local(int port)
{
	if (!transport::start()) return;
	sockets_started = true;
	listener = transport::listen_local(port);
	if (listener == invalid_socket) {
		write_log(_T("WinUAE GDB: cannot listen on 127.0.0.1:%d\n"), port);
		return;
	}
	write_log(_T("WinUAE GDB: listening on 127.0.0.1:%d\n"), port);
}
}

bool gdb_server_connected() { return session != nullptr; }
bool gdb_server_halted() { return session && session->stopped(); }

void gdb_server_close()
{
	restoring = false;
	disconnect_client();
	if (listener != invalid_socket) close_socket(listener);
	listener = invalid_socket;
	if (sockets_started) transport::stop();
	sockets_started = false;
	configured_port = 0;
}

void gdb_server_reset()
{
	// A requested checkpoint restore retains the connection until the CPU
	// reaches its restored boundary. Ordinary resets still detach clients.
	if (!restoring) gdb_server_close();
}

void gdb_server_begin_restore() { restoring = true; }

void gdb_server_restore_complete(bool success)
{
	if (!restoring) return;
	restoring = false;
	if (!session) return;
	// A state file can contain configuration from a different debug session.
	currprefs.debugging_features |= DEBUG_FEATURE_GDBSERVER;
	changed_prefs.debugging_features |= DEBUG_FEATURE_GDBSERVER;
	currprefs.gdb_port = changed_prefs.gdb_port = configured_port;
	session->complete_monitor(success);
	debug_gdb_target().interrupt();
}

void gdb_server_monitor_complete(bool success)
{
	if (session) session->complete_monitor(success);
}

void gdb_server_poll()
{
	if (polling || restoring) return;
	int port = (currprefs.debugging_features & DEBUG_FEATURE_GDBSERVER) ? currprefs.gdb_port : 0;
	// With the threaded CPU, debug() runs on either thread while the other
	// keeps executing, so remote breakpoint, watchpoint and detach handling
	// would change debugger and memory bank state under a running thread.
	if (port && currprefs.cpu_thread) {
		if (!threaded_cpu_warned)
			write_log(_T("WinUAE GDB: not available with the threaded CPU (cpu_threaded=true)\n"));
		threaded_cpu_warned = true;
		port = 0;
	} else {
		threaded_cpu_warned = false;
	}
	if (quit_program) port = 0;
	if (port != configured_port) {
		gdb_server_close();
		configured_port = port;
		if (port > 0 && port <= 65535) listen_local(port);
	}
	if (listener == invalid_socket) return;
	polling = true;
	if (client == invalid_socket) {
		client = transport::accept_client(listener);
		if (client != invalid_socket) {
			session.reset(new winuae_gdb::Session(debug_gdb_target()));
			debug_gdb_target().interrupt();
		}
	}
	if (session) {
		char buffer[4096];
		// Limit work per event pump so a busy client cannot starve the UI.
		for (int i = 0; i < 16 && session && !session->finished(); ++i) {
			int count = transport::read(client, buffer, sizeof(buffer));
			if (count > 0) session->receive(buffer, count);
			else {
				if (count != transport::retry) disconnect_client();
				break;
			}
		}
	}
	if (session) {
		pending += session->take_output();
		if (pending.size() > winuae_gdb::Session::packet_size * 4) disconnect_client();
	}
	if (session && !pending.empty()) {
		int count = transport::write(client, pending.data(), static_cast<int>(pending.size()));
		if (count > 0) pending.erase(0, count);
		else if (count != transport::retry) disconnect_client();
	}
	if (session && session->finished() && pending.empty()) disconnect_client();
	polling = false;
	retired_session.reset();
}

bool gdb_server_stop(const char* reason)
{
	if (!session) return false;
	session->stop(reason);
	pause_sound();
	while (session && session->stopped() && !quit_program) {
		gdb_server_poll();
		handle_msgpump(false);
		sleep_millis(1);
	}
	resume_sound();
	return true;
}
#endif
