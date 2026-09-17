#ifndef WINUAE_OD_UNIX_SERIAL_TX_H
#define WINUAE_OD_UNIX_SERIAL_TX_H

#include <stdint.h>

static inline int unix_serial_encode_tx(uint16_t serper, uint16_t serdat,
	uint8_t encoded[2])
{
	const uint8_t data = (uint8_t)serdat;
	if (serper & 0x8000) {
		encoded[0] = (uint8_t)(((serdat >> 8) & 1) | 0xa8);
		encoded[1] = data;
		return 2;
	}
	encoded[0] = data;
	return 1;
}

#endif
