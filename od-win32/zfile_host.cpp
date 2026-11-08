#include "sysconfig.h"
#include "sysdeps.h"
#include "zfile_host.h"

int host_internet_file_type (const TCHAR *name)
{
	if (!_tcsnicmp (name, _T("http://"), 7) || !_tcsnicmp (name, _T("https://"), 8))
		return 1;
	if (!_tcsnicmp (name, _T("ftp://"), 6))
		return -1;
	return 0;
}
#include <wininet.h>
#define INETBUFFERLEN 1000000
bool host_read_internet_file(const TCHAR *name, bool check_only, uae_u8 **out_data, int *out_size)
{
	static HINTERNET hi;
	HINTERNET i = NULL;
	TCHAR tmp[MAX_DPATH];
	DWORD ierr = 0;
	DWORD outbuf = sizeof tmp / sizeof (TCHAR);
	uae_u8 *data = 0;
	int bufferlen = INETBUFFERLEN;
	int datalen;
	DWORD didread;
	bool success = false;
	*out_data = NULL;
	*out_size = 0;

	tmp[0] = 0;
	if (!hi) {
		hi = InternetOpen (WINUAEAPPNAME, INTERNET_OPEN_TYPE_PRECONFIG_WITH_NO_AUTOPROXY, NULL, NULL, 0);
		if (hi == NULL) {
			write_log (_T("InternetOpen() failed, %d\n"), GetLastError ());
			return false;
		}
	}
	i = InternetOpenUrl (hi, name, NULL, 0, INTERNET_FLAG_NO_COOKIES, 0);
	if (i == NULL) {
		DWORD err = GetLastError ();
		if (err == ERROR_INTERNET_EXTENDED_ERROR)
			InternetGetLastResponseInfo (&ierr, tmp, &outbuf);
		write_log (_T("InternetOpenUrl(%s) failed %d (%d,%s)\n"), name, err, ierr, tmp);
		goto end;
	}

	if (host_internet_file_type (name) > 0) {
		DWORD statuscode;
		DWORD hindex = 0;
		DWORD size = sizeof statuscode;
		if (!HttpQueryInfo (i, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statuscode, &size, &hindex)) {
			DWORD err = GetLastError ();
			write_log (_T("HttpQueryInfo(%s) failed %d\n"), name, err);
			goto end;
		}
		if (statuscode != 200) {
			write_log (_T("HttpQueryInfo(%s)=%d\n"), name, statuscode);
			goto end;
		}
	}

	if (check_only) {
		success = true;
		goto end;
	}

	datalen = 0;
	data = xmalloc (uae_u8, bufferlen);
	for (;;) {
		if (!InternetReadFile (i, data + datalen, INETBUFFERLEN, &didread)) {
			DWORD err = GetLastError ();
			if (err == ERROR_INTERNET_EXTENDED_ERROR)
				InternetGetLastResponseInfo (&ierr, tmp, &outbuf);
			write_log (_T("InternetReadFile(%s) failed %d (%d,%s)\n"), name, err, ierr, tmp);
			break;
		}
		if (didread == 0)
			break;
		datalen += didread;
		if (datalen > bufferlen - INETBUFFERLEN) {
			bufferlen += INETBUFFERLEN;
			data = xrealloc (uae_u8, data, bufferlen);
			if (!data) {
				datalen = 0;
				break;
			}
		}
	}
	if (datalen > 0) {
		*out_size = datalen;
		*out_data = data;
		data = NULL;
		success = true;
	}
end:
	if (i)
		InternetCloseHandle (i);
	free (data);
	return success;
}
