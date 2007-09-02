
#include <windows.h>
#include <stdio.h>
#include <conio.h>

static char *pipename = "\\\\.\\pipe\\WinUAE";

static HANDLE p;
static volatile int threadmode_in;
static volatile int threadmode_out;

static DWORD WINAPI readroutine(void *parm)
{
    char buf[20000];
    DWORD ret, avail;

    for (;;) {
	Sleep(50);
	if (!threadmode_in)
	    continue;
	threadmode_out = 1;
	if (!PeekNamedPipe(p, NULL, 0, NULL, &avail, NULL)) {
	    printf ("PeekNamedPipe() failed, err=%d\n", GetLastError());
	    return 0;
	}
	if (avail > 0) {
	    if (!ReadFile(p, buf, sizeof buf, &ret, NULL)) {
		printf ("ReadFile() failed, err=%d\n", GetLastError());
		return 0;
	    }
	    printf("%s\n", buf);
	}
	threadmode_out = 0;
    }
}

int main(int argc, char *argv[])
{

    DWORD mode;
    DWORD tid;

    p = CreateFile(
	pipename,
	GENERIC_READ | GENERIC_WRITE,
	0,
	NULL,
	OPEN_EXISTING,
	0,
	NULL);
    if (p == INVALID_HANDLE_VALUE) {
	DWORD err = GetLastError();
	if (err == ERROR_PIPE_BUSY) {
	    printf ("Pipe '%s' busy\n", pipename);
	    return 0;
	}
	printf("Couldn't open pipe '%s' err=%d\n", pipename, err);
	return 0;
    }
    printf("Connected to '%s'\n", pipename);

    mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(p, &mode, NULL, NULL)) {
	printf("SetNamedPipeHandleState failed err=%d\n", GetLastError());
	return 0;
    }
    printf("ready\n");

    if (CreateThread(NULL, 0, &readroutine, NULL, 0, &tid) == NULL) {
	printf("Failed to create input thread\n");
	return 0;
    }
    threadmode_in = 1;

    for (;;) {
	DWORD ret;
	char inbuf[4000];
	inbuf[0] = 0;
	fgets(inbuf, sizeof(inbuf), stdin);
	if (strlen(inbuf) == 0 || inbuf[0] == 10)
	    break;
	threadmode_in = 0;
	while (threadmode_out)
	    Sleep(10);
	if (!WriteFile(p, inbuf, strlen (inbuf) + 1, &ret, NULL)) {
	    printf("WriteFile() failed, err=%d\n", GetLastError());
	    return 0;
	}
	threadmode_in = 1;
    }

    return 0;
}


