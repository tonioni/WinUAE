
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <windows.h>
#include "fdrawcmd.h"
#include "diskutil.h"

#define TRACK_SIZE 16384
static int longread = 1;
#define MAX_RETRIES 10

static UBYTE writebuffer[11 * 512];
static UBYTE writebuffer_ok[11];

static BYTE *trackbuffer;
static HANDLE h = INVALID_HANDLE_VALUE;
static FILE *fout;

static int checkversion(void)
{
	DWORD version = 0;
	DWORD ret;

	h = CreateFile("\\\\.\\fdrawcmd", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	DeviceIoControl(h, IOCTL_FDRAWCMD_GET_VERSION, NULL, 0, &version, sizeof version, &ret, NULL);
	CloseHandle(h);
	if (!version) {
		printf("fdrawcmd.sys is not installed, see: http://simonowen.com/fdrawcmd/\n");
		return 0;
	}
	if (HIWORD(version) != HIWORD(FDRAWCMD_VERSION)) {
		printf("fdrawcmd.sys major version mismatch %d <> %d\n",
			HIWORD(version), HIWORD(FDRAWCMD_VERSION));
		return 0;
	}
	return version;
}

static void closedevice(void)
{
	if (h == INVALID_HANDLE_VALUE)
		return;
	CloseHandle(h);
}

static int opendevice(void)
{
	HANDLE h;
	DWORD ret;
	BYTE b;

	h = CreateFile("\\\\.\\fdraw0", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	b = 0; // 500Kbps 
	if (!DeviceIoControl(h, IOCTL_FD_SET_DATA_RATE, &b, sizeof b, NULL, 0, &ret, NULL)) {
		printf("IOCTL_FD_SET_DATA_RATE=%d failed err=%d\n", b, GetLastError());
		closedevice();
		return 0;
	}
	return 1;
}

#if 1
static int readraw(int cyl, int head)
{
	FD_RAW_READ_PARAMS rrp;
	FD_SEEK_PARAMS sp;
	DWORD ret;

	sp.cyl = cyl;
	sp.head = head;
	if (!DeviceIoControl(h, IOCTL_FDCMD_SEEK, &sp, sizeof sp, NULL, 0, &ret, NULL)) {
		printf("IOCTL_FDCMD_SEEK failed cyl=%d, err=%d\n", sp.cyl, GetLastError());
		return 0;
	}

	rrp.flags = FD_OPTION_MFM;
	rrp.size = longread == 1 ? 7 : 8;
	rrp.head = head;
	memset (trackbuffer, 0, TRACK_SIZE * longread);
	if (!DeviceIoControl(h, IOCTL_FD_RAW_READ_TRACK * longread,  &rrp, sizeof rrp,
		trackbuffer, TRACK_SIZE * longread, &ret, NULL)) {
		printf("IOCTL_FD_RAW_READ_TRACK failed, err=%d\n", GetLastError());
		return 0;
	}

	return 1;
}
#else
static int readraw(int cyl, int head)
{
	FILE *f;

	if (fopen_s(&f, "f:\\amiga\\amiga.dat", "rb"))
		return 0;
	fseek(f, (cyl * 2 + head) * 16384, SEEK_SET);
	fread(trackbuffer, TRACK_SIZE, 1, f);
	fclose(f);
	return 1;
}
#endif

static void readloop(char *fname)
{
	int trk, i, j, sec;
	int errsec, oktrk, retr;
	time_t t = time(0);

	if (fopen_s(&fout, fname, "wb")) {
		printf("Failed to open '%s'\n", fname);
		return;
	}
	errsec = oktrk = retr = 0;
	for (trk = 0; trk < 2 * 80; trk++) {
		printf ("Track %d: processing started..\n", trk);
		memset (writebuffer_ok, 0, sizeof writebuffer_ok);
		memset (writebuffer, 0, sizeof writebuffer);
		for (j = 0; j < MAX_RETRIES; j++) {
			if (j > 0)
				printf("Retrying.. (%d/%d)\n", j, MAX_RETRIES - 1);
			if (!readraw(trk / 2, trk % 2)) {
				printf("Raw read error, possible reasons:\nMissing second drive or your hardware only supports single drive.\nOperation aborted.\n");
				return;
			}
			isamigatrack(trackbuffer, TRACK_SIZE * longread, writebuffer, writebuffer_ok, trk);
			sec = 0;
			for (i = 0; i < 11; i++) {
				if (writebuffer_ok[i])
					sec++;
			}
			if (sec == 11)
				break;
			retr++;
		}
		errsec += 11 - sec;
		if (j == MAX_RETRIES) {
			printf("Track %d: read error or non-AmigaDOS formatted track (%d/11 sectors ok)\n", trk, sec);
		} else {
			oktrk++;
			printf("Track %d: all sectors ok (%d retries)\n", trk, j);
		}
		fwrite(writebuffer, 11 * 512, 1, fout);
	}
	fclose(fout);
	t = time(0) - t;
	printf ("Completed. %02.2dm%02.2ds, %d/160 tracks read without errors, %d retries, %d faulty sectors\n",
		t / 60, t % 60, oktrk, retr, errsec);
}

int main(int argc, char *argv[])
{
	DWORD ver;
	char *fname = NULL;
	int i;

	for (i = 1; i < argc; i++) {
		if (!_stricmp(argv[i], "-l")) {
			longread = 2;
			continue;
		}
		break;
	}
	if (argc < 2 || i >= argc) {
	    printf("adiskutil.exe [-l] <name of new disk image>\n");
	    return 0;
	}
	fname = argv[i];
	ver = checkversion();
	if (!ver)
		return 0;
	printf ("fdrawcmd.sys %x detected. Read size %d\n", ver, TRACK_SIZE * longread);
	trackbuffer = VirtualAlloc(NULL, TRACK_SIZE * 4, MEM_COMMIT, PAGE_READWRITE);
	if (opendevice()) {
		readloop(fname);
	}
	closedevice();
	VirtualFree(trackbuffer, 0, MEM_RELEASE);
	return 0;
}
