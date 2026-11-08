#ifndef UAE_GDB_TRANSPORT_H
#define UAE_GDB_TRANSPORT_H

#include <cstdint>

namespace winuae_gdb_transport {
using Socket = uintptr_t;
constexpr Socket invalid_socket = static_cast<Socket>(-1);
constexpr int retry = -1;
constexpr int error = -2;

bool start();
void stop();
// Listen on loopback only. Both listening and accepted sockets are nonblocking.
Socket listen_local(int port);
Socket accept_client(Socket listener);
void close_socket(Socket socket);
// Positive byte count, zero for EOF, retry for a transient condition,
// or error for a failed connection. A write cannot raise SIGPIPE.
int read(Socket socket, char *buffer, int size);
int write(Socket socket, const char *buffer, int size);
}

#endif /* UAE_GDB_TRANSPORT_H */
