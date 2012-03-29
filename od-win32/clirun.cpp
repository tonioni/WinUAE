#if 0
#define XFD 0

#include <windows.h>

#include <stdio.h>
#include <tchar.h>

static int runthread = 1;
static HANDLE stdin_save, stdout_save, stderr_save;
static PROCESS_INFORMATION pi;


static DWORD WINAPI pipethread (LPVOID pipewr)
{
	TCHAR buff[256];
	DWORD read, wrote;
	HANDLE pipewrite = (HANDLE)pipewr;

	while (runthread) {
		read = 0;
		ReadConsole (stdin_save, buff, 1, &read, NULL);
		buff[read] = 0;
		if (read > 0) {
			if (!WriteFile (pipewrite, buff, read * sizeof (TCHAR), &wrote, NULL)) {
				if (GetLastError () == ERROR_NO_DATA)
					break;
			}
		}
	}
	return 0;
}

static void HandleOutput (HANDLE piperead)
{
	TCHAR buffer[256];
	DWORD read, wrote;

	for (;;) {
		read = 0;
		if (!ReadFile (piperead, buffer, sizeof buffer, &read, NULL)) {
			if (GetLastError () == ERROR_BROKEN_PIPE)
				break;
		}
		if (read > 0) {
			WriteConsole (stdout_save, buffer, read / sizeof (TCHAR), &wrote, NULL);
		}
	}
}

static BOOL WINAPI ctrlhandler (DWORD type)
{
	if (pi.hProcess)
		TerminateProcess (pi.hProcess, 0);
	ExitProcess (0);
	return TRUE;
}

#define conpar _T("-console")

static int runmain (int argc, wchar_t *argv[])
{
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	STARTUPINFO si;
	TCHAR *cmd, *parms2;
	int len, parmlen, i;
	HANDLE cp;
	HANDLE out_rd = NULL, out_wr = NULL, out_rd_tmp = NULL;
	HANDLE in_rd = NULL, in_wr = NULL, in_wr_tmp = NULL;
	HANDLE err_wr;
	DWORD tid;
	HANDLE thread;
	SECURITY_ATTRIBUTES sa;

	len = _tcslen (argv[0]);
	if (len < 4)
		return 0;
	cmd = (TCHAR*)malloc ((len + 4 + 1) * sizeof (TCHAR));
	_tcscpy (cmd, argv[0]);
	if (_tcsicmp (cmd + len - 4, _T(".com")))
		_tcscat (cmd + len, _T(".exe"));
	else
		_tcscpy (cmd + len - 4, _T(".exe"));

	parmlen = 0;
	for (i = 1; i < argc; i++) {
		if (parmlen > 0)
			parmlen ++;
		parmlen += 1 + _tcslen (argv[i]) + 1;
	}
	parms2 = (TCHAR*)malloc ((_tcslen (cmd) + 1 + parmlen + 1 + _tcslen (conpar) + 1) * sizeof (TCHAR));
	_tcscpy (parms2, cmd);
	_tcscat (parms2, _T(" "));
	_tcscat (parms2, conpar);
	for (i = 1; i < argc; i++) {
		int isspace = 0;
		_tcscat (parms2, _T(" "));
		if (_tcschr (argv[i], ' '))
			isspace = 1;
		if (isspace)
			_tcscat (parms2, _T("\""));
		_tcscat (parms2, argv[i]);
		if (isspace)
			_tcscat (parms2, _T("\""));
	}

	cp = GetCurrentProcess ();
	sa.nLength = sizeof sa;
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;

	stdout_save = GetStdHandle (STD_OUTPUT_HANDLE);
	stdin_save = GetStdHandle (STD_INPUT_HANDLE);
	stderr_save = GetStdHandle (STD_ERROR_HANDLE);

	//SetConsoleMode (stdin_save, ENABLE_PROCESSED_INPUT|ENABLE_LINE_INPUT|ENABLE_ECHO_INPUT|ENABLE_PROCESSED_OUTPUT);
	SetConsoleMode (stdin_save, ENABLE_PROCESSED_INPUT|ENABLE_PROCESSED_OUTPUT);
	SetConsoleCP (65001);
	SetConsoleOutputCP (65001);
	if (GetConsoleScreenBufferInfo (stdout_save, &csbi)) {
		if (csbi.dwMaximumWindowSize.Y < 900) {
			csbi.dwMaximumWindowSize.Y = 900;
			SetConsoleScreenBufferSize (stdout_save, csbi.dwMaximumWindowSize);
		}
	}

	CreatePipe (&out_rd_tmp, &out_wr, &sa, 0);
	CreatePipe (&in_rd, &in_wr_tmp, &sa, 0);

	DuplicateHandle (cp, out_wr, cp, &err_wr, 0, TRUE, DUPLICATE_SAME_ACCESS);
	DuplicateHandle (cp, out_rd_tmp, cp, &out_rd, 0, FALSE, DUPLICATE_SAME_ACCESS);
	DuplicateHandle (cp, in_wr_tmp, cp, &in_wr, 0, FALSE, DUPLICATE_SAME_ACCESS);

	CloseHandle (out_rd_tmp);
	CloseHandle (in_wr_tmp);

	memset (&si, 0, sizeof si);
	si.cb = sizeof si;
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = in_rd;
	si.hStdOutput = out_wr;
	si.hStdError = err_wr;

	SetConsoleCtrlHandler (&ctrlhandler, TRUE);

	if (!CreateProcess (cmd, parms2,
		NULL, NULL, TRUE,
		CREATE_SUSPENDED | CREATE_NEW_CONSOLE | GetPriorityClass (GetCurrentProcess ()),
		NULL, NULL, &si, &pi)) {
			_tprintf (_T("CreateProcess(%s) failed\n"), cmd);
			goto end;
	}

	CloseHandle (out_wr);
	CloseHandle (in_rd);
	CloseHandle (err_wr);

	thread = CreateThread (NULL, 0, pipethread, (LPVOID)in_wr, 0, &tid);

	ResumeThread (pi.hThread);

	HandleOutput (out_rd);
	runthread = 0;
	CloseHandle (stdin_save);
	WaitForSingleObject (thread, INFINITE);

	CloseHandle (out_rd);
	CloseHandle (in_wr);

	CloseHandle (pi.hProcess);
	CloseHandle (pi.hThread);
end:
	free (parms2);
	free (cmd);
	return 0;
}


static int runxfd(int argc,wchar_t *argv[])
{
	STARTUPINFO si = { 0 };
	PROCESS_INFORMATION pi = { 0 };
	TCHAR *src, *dst;
	TCHAR *parms[5];
	FILE *f;

	_tprintf(_T("uaexfd 0.1b by Toni Wilen (c)2010\n"));
	if (argc < 2) {
		_tprintf(_T("uaexfd <source> [<destination>]"));
		return 0;
	}
	if (GetFileAttributes(_T("xfd.uae")) == INVALID_FILE_ATTRIBUTES) {
		_tprintf(_T("xfd.uae missing\n"));
		return 0;
	}
	if (GetFileAttributes(_T("uaexfd.zip")) == INVALID_FILE_ATTRIBUTES) {
		_tprintf(_T("uaexfd.zip missing\n"));
		return 0;
	}
	dst = src = argv[1];
	if (GetFileAttributes(src) == INVALID_FILE_ATTRIBUTES) {
		_tprintf(_T("can't open '%s'\n"), src);
		return 0;
	}
	if (argc >= 3)
		dst = argv[2];
	DeleteFile(_T("xfd-in-file.dat"));	
	DeleteFile(_T("xfd-out-file.dat"));
	DeleteFile(_T("xfd-out-text.txt"));
	CopyFile(src, _T("xfd-in-file.dat"), FALSE);
	parms[0] = _T("winuae.com");
	parms[1] = _T("-f");
	parms[2] = _T("xfd.uae");
	parms[3] = _T("-datapath");
	parms[4] = _T(".");
	parms[5] = NULL;
	if (!runmain(5, parms)) {
		if (GetFileAttributes(_T("xfd-out-file.dat")) != INVALID_FILE_ATTRIBUTES) {
			DeleteFile(dst);
			MoveFile(_T("xfd-out-file.dat"), dst);
		}
		f = _tfopen(_T("xfd-out-text.txt"), _T("rb"));
		if (f) {
			char tmp[1000];
			while (fgets(tmp, sizeof tmp, f)) {
				if (strlen(tmp) <= 1)
					continue;
				for (int i = 0; tmp[i]; i++) {
					if (tmp[i] == -101 || (tmp[i] == 27 && tmp[i + 1] == '[')) {
						int j = i + 1;
						if (tmp[i] == 27)
							j++;
						while (tmp[j] > 0 && tmp[j] < 0x40)
							j++;
						if (tmp[j])
							j++;
						memmove (tmp + i, tmp + j, strlen(tmp + j) + 1);
					}
				}
				if (tmp[strlen(tmp)-1] == 10)
					tmp[strlen(tmp)-1] = 0;
				printf("%s\n", tmp);
			}
			fclose(f);
		} else {
			_tprintf(_T("startup failed\n"));
		}
	}
	DeleteFile(_T("xfd-in-file.dat"));	
	DeleteFile(_T("xfd-out-file.dat"));
	DeleteFile(_T("xfd-out-text.txt"));
	DeleteFile(_T("winuaebootlog.txt"));
	RemoveDirectory(_T("Host"));
	RemoveDirectory(_T("Hardware"));
	return 0;
}

int wmain (int argc, wchar_t *argv[], wchar_t *envp[])
{
#if XFD
	return runxfd (argc, argv);
#else
	return runmain (argc, argv);
#endif
}

#endif
