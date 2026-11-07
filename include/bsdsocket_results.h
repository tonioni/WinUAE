#ifndef UAE_BSDSOCKET_RESULTS_H
#define UAE_BSDSOCKET_RESULTS_H

#include "uae/types.h"

typedef struct TrapContext TrapContext;
struct socketbase;

bool bsdsock_copy_hostent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int address_type,
	int address_length, const char *const *addresses);
bool bsdsock_copy_protoent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int protocol);
/* port is in host byte order; address bytes above are in network order. */
bool bsdsock_copy_servent(TrapContext *ctx, struct socketbase *sb,
	const char *name, const char *const *aliases, int port, const char *protocol);

#endif
