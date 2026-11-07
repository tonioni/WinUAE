#include "sysconfig.h"
#include "sysdeps.h"
#include "serial_helpers.h"

speed_t serial_baud_to_speed(int baud)
{
	switch (baud) {
	case 300: return B300;
	case 1200: return B1200;
	case 2400: return B2400;
	case 4800: return B4800;
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
#ifdef B230400
	case 230400: return B230400;
#endif
	default: return B9600;
	}
}

bool serial_parse_tcp_spec(const TCHAR *spec, std::string *host, std::string *port, bool *waitmode)
{
	std::string s = spec ? spec : "";
	*waitmode = false;
	if (s.compare(0, 2, "//") == 0) {
		s.erase(0, 2);
	}
	size_t slash = s.find('/');
	if (slash != std::string::npos) {
		std::string opt = s.substr(slash + 1);
		s.erase(slash);
		if (!strcasecmp(opt.c_str(), "wait")) {
			*waitmode = true;
		}
	}
	*host = "127.0.0.1";
	*port = "1234";
	if (s.empty()) {
		return true;
	}
	size_t colon = s.rfind(':');
	if (colon == std::string::npos) {
		bool digits = true;
		for (char c : s) {
			if (!isdigit((unsigned char)c)) {
				digits = false;
				break;
			}
		}
		if (digits) {
			*port = s;
		} else {
			*host = s;
		}
		return true;
	}
	if (colon > 0) {
		*host = s.substr(0, colon);
	}
	if (colon + 1 < s.size()) {
		*port = s.substr(colon + 1);
	}
	return true;
}
