// Host fault handling around the legacy module scanners.
#include "sysconfig.h"
#include "sysdeps.h"
#include "moduleripper.h"

#ifdef PROWIZARD
void host_prowizard_search(unsigned char *data, int size)
{
#ifdef _MSC_VER
	__try {
		prowizard_search(data, size);
	} __except(EXCEPTION_EXECUTE_HANDLER) {
		write_log(_T("prowizard scan crashed\n"));
	}
#else
	prowizard_search(data, size);
#endif
}
#endif
