/* Optional WinUAE remote debugger. All calls run on the emulation thread. */
#ifndef WINUAE_GDB_SERVER_H
#define WINUAE_GDB_SERVER_H

#define DEBUG_FEATURE_GDBSERVER (1 << 2)

#ifdef DEBUGGER
namespace winuae_gdb { class Target; }
winuae_gdb::Target& debug_gdb_target();
void gdb_server_poll();
void gdb_server_close();
bool gdb_server_connected();
bool gdb_server_stop(const char* reason);
#else
static inline void gdb_server_poll() {}
static inline void gdb_server_close() {}
static inline bool gdb_server_connected() { return false; }
static inline bool gdb_server_stop(const char*) { return false; }
#endif
#endif
