#include "sysconfig.h"
#include "sysdeps.h"

#define MFMMASK 0x55555555
static uae_u32 getmfmlong (uae_u16 * mbuf)
{
	return (uae_u32)(((*mbuf << 16) | *(mbuf + 1)) & MFMMASK);
}

#define FLOPPY_WRITE_LEN 6250

static int drive_write_adf_amigados (uae_u16 *mbuf, uae_u16 *mend, uae_u8 *writebuffer, uae_u8 *writebuffer_ok, int track)
{
	int i;
	uae_u32 odd, even, chksum, id, dlong;
	uae_u8 *secdata;
	uae_u8 secbuf[544];

	mend -= (4 + 16 + 8 + 512);
	for (;;) {
		int trackoffs;

	/* all sectors complete? */
		for (i = 0; i < 11; i++) {
			if (!writebuffer_ok[i])
				break;
		}
		if (i == 11)
			return 0;

		do {
			while (*mbuf++ != 0x4489) {
				if (mbuf >= mend) {
					write_log (L"* track %d, unexpected end of data\n", track);
					return 1;
				}
			}
		} while (*mbuf++ != 0x4489);

		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		id = (odd << 1) | even;

		trackoffs = (id & 0xff00) >> 8;
		if (trackoffs > 10) {
			write_log (L"* track %d, corrupt sector number %d\n", track, trackoffs);
			goto next;
		}
		/* this sector is already ok? */
		if (writebuffer_ok[trackoffs])
			goto next;

		chksum = odd ^ even;
		for (i = 0; i < 4; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 8);
			mbuf += 2;

			dlong = (odd << 1) | even;
			if (dlong) {
				write_log (L"* track %d, sector %d header crc error\n", track, trackoffs);
				goto next;
			}
			chksum ^= odd ^ even;
		} /* could check here if the label is nonstandard */
		mbuf += 8;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		if (((odd << 1) | even) != chksum || ((id & 0x00ff0000) >> 16) != (uae_u32)track) return 3;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		chksum = (odd << 1) | even;
		secdata = secbuf + 32;
		for (i = 0; i < 128; i++) {
			odd = getmfmlong (mbuf);
			even = getmfmlong (mbuf + 256);
			mbuf += 2;
			dlong = (odd << 1) | even;
			*secdata++ = (uae_u8)(dlong >> 24);
			*secdata++ = (uae_u8)(dlong >> 16);
			*secdata++ = (uae_u8)(dlong >> 8);
			*secdata++ = (uae_u8)dlong;
			chksum ^= odd ^ even;
		}
		mbuf += 256;
		if (chksum) {
			write_log (L"* track %d, sector %d data crc error\n", track, trackoffs);
			goto next;
		}
		memcpy (writebuffer + trackoffs * 512, secbuf + 32, 512);
		writebuffer_ok[trackoffs] = 0xff;
		continue;
next:
		mbuf += 8;
	}
}

/* search and align to 0x4489 WORDSYNC markers */
int isamigatrack(uae_u16 *amigamfmbuffer, uae_u8 *mfmdata, int len, uae_u8 *writebuffer, uae_u8 *writebuffer_ok, int track)
{
	uae_u16 *dst = amigamfmbuffer;
	int shift, syncshift, sync;
	uae_u32 l;
	uae_u16 w;

	len *= 8;
	sync = syncshift = shift = 0;
	while (len--) {
		l = (mfmdata[0] << 16) | (mfmdata[1] << 8) | (mfmdata[2] << 0);
		w = (uae_u16)(l >> (8 - shift));
		if (w == 0x4489) {
			sync = 1;
			syncshift = 0;
		}
		if (sync) {
			if (syncshift == 0) *dst++ = w;
			syncshift++;
			if (syncshift == 16) syncshift = 0;
		}
		shift++;
		if (shift == 8) {
			mfmdata++;
			shift = 0;
		}
	}
	if (sync)
		return drive_write_adf_amigados (amigamfmbuffer, dst, writebuffer, writebuffer_ok, track);
	return -1;
}