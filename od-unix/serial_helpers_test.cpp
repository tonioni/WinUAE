#include "sysconfig.h"
#include "sysdeps.h"
#include "serial_helpers.h"

static void check(bool ok)
{
	if (!ok) {
		fprintf(stderr, "Serial helper regression\n");
		exit(1);
	}
}

int main(void)
{
	struct {
		const TCHAR *input;
		const char *host, *port;
		bool wait;
	} cases[] = {
		{ NULL, "127.0.0.1", "1234", false },
		{ "", "127.0.0.1", "1234", false },
		{ "//", "127.0.0.1", "1234", false },
		{ "51234", "127.0.0.1", "51234", false },
		{ "localhost", "localhost", "1234", false },
		{ "//localhost:51234/wAiT", "localhost", "51234", true },
		{ ":51234", "127.0.0.1", "51234", false },
		{ "localhost:", "localhost", "1234", false },
		{ "//:51234/wait", "127.0.0.1", "51234", true },
		{ "//localhost:51234/unknown", "localhost", "51234", false },
		{ "localhost:service", "localhost", "service", false }
	};
	std::string host, port;
	bool wait = true;
	for (const auto &c : cases) {
		check(serial_parse_tcp_spec(c.input, &host, &port, &wait));
		check(host == c.host && port == c.port && wait == c.wait);
	}
	check(serial_baud_to_speed(9600) == B9600);
	check(serial_baud_to_speed(115200) == B115200);
	check(serial_baud_to_speed(0) == B9600);
	check(serial_baud_to_speed(-1) == B9600);
	check(serial_baud_to_speed(12345) == B9600);
#ifdef B230400
	check(serial_baud_to_speed(230400) == B230400);
#else
	check(serial_baud_to_speed(230400) == B9600);
#endif
	puts("Serial helper tests passed");
	return 0;
}
