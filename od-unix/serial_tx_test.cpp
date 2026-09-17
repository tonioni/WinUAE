#include "serial_tx.h"

#include <stdio.h>

static bool require_encoding(const char *label, uint16_t serper,
	uint16_t serdat, int expected_size, uint8_t first, uint8_t second)
{
	uint8_t encoded[2] = { 0, 0 };
	const int size = unix_serial_encode_tx(serper, serdat, encoded);
	if (size == expected_size && encoded[0] == first
		&& (size == 1 || encoded[1] == second)) {
		return true;
	}
	fprintf(stderr,
		"%s: expected %d bytes %02x %02x, got %d bytes %02x %02x\n",
		label, expected_size, first, second, size, encoded[0], encoded[1]);
	return false;
}

int main()
{
	bool ok = true;

	ok = require_encoding("8-bit with SERDAT bit 8", 0, 0x161, 1,
		0x61, 0) && ok;
	ok = require_encoding("9-bit clear", 0x8000, 0x061, 2,
		0xa8, 0x61) && ok;
	ok = require_encoding("9-bit set", 0x8000, 0x161, 2,
		0xa9, 0x61) && ok;

	return ok ? 0 : 1;
}
