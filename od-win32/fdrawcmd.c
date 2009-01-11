// adfread by Toni Wilen <twilen@winuae.net>
//
// uses fdrawcmd.sys by Simon Owen <simon@simonowen.com>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>

#include <windows.h>
#include "fdrawcmd.h"
#include "diskutil.h"

#define TRACK_SIZE 16384
#define MAX_RETRIES 50

#define SECTORS 11
#define CYLINDERS 80
#define TRACKS (CYLINDERS * 2)
#define BLOCKSIZE 512

static UBYTE writebuffer[SECTORS * BLOCKSIZE];

static BYTE *trackbuffer;
static HANDLE h = INVALID_HANDLE_VALUE;

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

static int seek(int cyl, int head)
{
	FD_SEEK_PARAMS sp;
	DWORD ret;

	sp.cyl = cyl;
	sp.head = head;
	if (!DeviceIoControl(h, IOCTL_FDCMD_SEEK, &sp, sizeof sp, NULL, 0, &ret, NULL)) {
		printf("IOCTL_FDCMD_SEEK failed cyl=%d, err=%d\n", sp.cyl, GetLastError());
		return 0;
	}
	return 1;
}

#if 1
static int readraw(int cyl, int head)
{
	FD_RAW_READ_PARAMS rrp;
	DWORD ret;

	if (!seek(cyl, head))
		return 0;

	rrp.flags = FD_OPTION_MFM;
	rrp.head = head;
	rrp.size = 7;
	memset (trackbuffer, 0, TRACK_SIZE);
	if (!DeviceIoControl(h, IOCTL_FD_RAW_READ_TRACK,  &rrp, sizeof rrp, trackbuffer, TRACK_SIZE, &ret, NULL)) {
		printf("IOCTL_FD_RAW_READ_TRACK failed, err=%d\n", GetLastError());
		return 0;
	}

	return 1;
}
#else
static int readraw(int cyl, int head)
{
	FILE *f;

	if (!(f = fopen("f:\\amiga\\amiga.dat", "rb")))
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
	static FILE *fout, *ferr;
	char *fnameerr;
	UBYTE writebuffer_ok[SECTORS];
	int fromscratch = 0;

	memset (writebuffer, 0, sizeof writebuffer);
	fout = fopen(fname,"r+b");
	if (!fout) {
		if (!(fout = fopen(fname, "w+b"))) {
			printf("Failed to create '%s'\n", fname);
			return;
		}
		/* pre-create the image */
		for (i = 0; i < (sizeof writebuffer) / 8; i++)
			memcpy (writebuffer + i * 8, "*NULADF*", 8);
		for (i = 0; i < TRACKS; i++)
			fwrite(writebuffer, SECTORS * BLOCKSIZE, 1, fout);
		fromscratch = 1;
	}

	/* create error status file */
	memset (writebuffer, 0, sizeof writebuffer);
	fnameerr = malloc (strlen (fname) + 10);
	sprintf (fnameerr, "%s.status", fname);
	ferr = fopen(fnameerr, "r+b");
	if (!ferr) {
		ferr = fopen(fnameerr, "w+b");
		fromscratch = 1;
	}
	if (ferr && fromscratch)
		fwrite(writebuffer, SECTORS * TRACKS, 1, ferr);

	errsec = oktrk = retr = 0;
	for (trk = 0; trk < TRACKS; trk++) {

		printf ("Track %d: processing started..\n", trk);
		memset (writebuffer_ok, 0, sizeof writebuffer_ok);
		/* fill decoded trackbuffer with easily detectable error code */
		for (i = 0; i < (sizeof writebuffer) / 8; i++)
			memcpy (writebuffer + i * 8, "*ERRADF*", 8);

		/* read possible old track */
		if (ferr) {
			if (!fseek(ferr, trk * SECTORS, SEEK_SET))
				fread (writebuffer_ok, SECTORS, 1, ferr);
			if (!fseek(fout, SECTORS * BLOCKSIZE * trk, SEEK_SET))
				fread (writebuffer, SECTORS * BLOCKSIZE, 1, fout);
		}

		j = 0;
		for (;;) {
			/* all sectors ok? */
			sec = 0;
			for (i = 0; i < SECTORS; i++) {
				if (writebuffer_ok[i])
					sec++;
			}
			if (sec == SECTORS || j >= MAX_RETRIES)
				break;

			if (j > 0)
				printf("Retrying.. (%d of max %d), %d/%d sectors ok\n", j, MAX_RETRIES - 1, sec, SECTORS);
			/* read raw track */
			if (!readraw(trk / 2, trk % 2)) {
				printf("Raw read error, possible reasons:\nMissing second drive or your hardware only supports single drive.\nOperation aborted.\n");
				return;
			}
			/* decode track (ignores already ok sectors) */
			isamigatrack(trackbuffer, TRACK_SIZE, writebuffer, writebuffer_ok, trk);
			retr++;
			if ((retr % 10) == 0)
				seek(trk == 0 ? 2 : 0, 0);
			j++;
		}
		errsec += SECTORS - sec;
		if (j == MAX_RETRIES) {
			printf("Track %d: read error or non-AmigaDOS formatted track (%d/%d sectors ok)\n", trk, sec, SECTORS);
		} else {
			oktrk++;
			printf("Track %d: all sectors ok (%d retries)\n", trk, j);
		}
		/* write decoded track */
		fseek(fout, SECTORS * BLOCKSIZE * trk, SEEK_SET);
		fwrite(writebuffer, SECTORS * BLOCKSIZE, 1, fout);
		/* write sector status */
		if (ferr) {
			fseek(ferr, trk * SECTORS, SEEK_SET);
			fwrite (writebuffer_ok, SECTORS, 1, ferr);
		};

	}
	fclose(fout);
	if (ferr) {
		fclose(ferr);
		if (oktrk >= TRACKS)
			unlink(fnameerr);
	}
	free(fnameerr);
	t = time(0) - t;
	printf ("Completed. %02dm%02ds, %d/160 tracks read without errors, %d retries, %d faulty sectors\n",
		t / 60, t % 60, oktrk, retr, errsec);
}

int main(int argc, char *argv[])
{
	DWORD ver;

	if (argc < 2) {
	    printf("adfread 1.1\nUsage: adfread.exe <name of new disk image>\n");
	    return 0;
	}

	ver = checkversion();
	if (!ver)
		return 0;
	printf ("adfread 1.1: fdrawcmd.sys %x detected\n", ver);
	trackbuffer = VirtualAlloc(NULL, TRACK_SIZE * 2, MEM_COMMIT, PAGE_READWRITE);
	if (opendevice()) {
		readloop(argv[1]);
	}
	closedevice();
	VirtualFree(trackbuffer, 0, MEM_RELEASE);
	return 0;
}
