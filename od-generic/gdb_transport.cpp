// Native transport for the loopback debugger. No emulated state lives here.
#include "sysconfig.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#endif
#include "gdb_transport.h"

namespace winuae_gdb_transport {
namespace {
#ifdef _WIN32
using NativeSocket = SOCKET;
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
bool nonblocking(NativeSocket s)
{
	u_long one = 1;
	return ioctlsocket(s, FIONBIO, &one) == 0;
}
#else
using NativeSocket = int;
bool would_block()
{
	return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}
bool nonblocking(NativeSocket s)
{
	int flags = fcntl(s, F_GETFL, 0);
	return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

int result(int count)
{
	return count >= 0 ? count : would_block() ? retry : error;
}
}

bool start()
{
#ifdef _WIN32
	WSADATA data;
	return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
	return true;
#endif
}

void stop()
{
#ifdef _WIN32
	WSACleanup();
#endif
}

void close_socket(Socket socket)
{
#ifdef _WIN32
	closesocket(static_cast<NativeSocket>(socket));
#else
	close(static_cast<NativeSocket>(socket));
#endif
}

Socket listen_local(int port)
{
	NativeSocket listener = socket(AF_INET, SOCK_STREAM, 0);
	if (static_cast<Socket>(listener) == invalid_socket) return invalid_socket;
	int one = 1;
#ifdef _WIN32
	setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		reinterpret_cast<const char*>(&one), sizeof(one));
#else
	setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(static_cast<unsigned short>(port));
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (!nonblocking(listener) || bind(listener,
		reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 1)) {
		close_socket(static_cast<Socket>(listener));
		return invalid_socket;
	}
	return static_cast<Socket>(listener);
}

Socket accept_client(Socket socket)
{
	NativeSocket client = accept(static_cast<NativeSocket>(socket), nullptr, nullptr);
	if (static_cast<Socket>(client) == invalid_socket) return invalid_socket;
	if (!nonblocking(client)) {
		close_socket(static_cast<Socket>(client));
		return invalid_socket;
	}
#ifdef SO_NOSIGPIPE
	int one = 1;
	setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
	return static_cast<Socket>(client);
}

int read(Socket socket, char *buffer, int size)
{
	return result(static_cast<int>(recv(static_cast<NativeSocket>(socket), buffer, size, 0)));
}

int write(Socket socket, const char *buffer, int size)
{
	int flags = 0;
#ifdef MSG_NOSIGNAL
	flags = MSG_NOSIGNAL;
#endif
	return result(static_cast<int>(send(static_cast<NativeSocket>(socket), buffer, size, flags)));
}
}
