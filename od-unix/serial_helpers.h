#ifndef UAE_UNIX_SERIAL_HELPERS_H
#define UAE_UNIX_SERIAL_HELPERS_H

#include <string>
#include <termios.h>

speed_t serial_baud_to_speed(int baud);
bool serial_parse_tcp_spec(const TCHAR *spec, std::string *host,
	std::string *port, bool *waitmode);

#endif
