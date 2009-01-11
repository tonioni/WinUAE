// adfread by Toni Wilen <twilen@winuae.net>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "diskutil.h"

#define LONGEST_TRACK 16000
#define LONG_TRACK 12800
#define MAX_CYLINDERS 83
#define MAX_TRACKS 2*MAX_CYLINDERS

typedef enum { TRACK_AMIGADOS, TRACK_RAW } image_tracktype;
typedef enum { ADF_NORMAL, ADF_EXT1, ADF_EXT2 } drive_filetype;

static UWORD amigamfmbuffer[LONGEST_TRACK];
static UBYTE tmpmfmbuffer[LONGEST_TRACK];
static UBYTE conversiontable[MAX_TRACKS];
static int amigatracklength;

struct trackid {
	int trackbytes;
	int trackbits;
	image_tracktype type;
};
static drive_filetype imagetype;
static struct trackid tracks[MAX_TRACKS];

#if 0
static int bitshiftcompare(UBYTE *src,int bit,int len,UBYTE *comp)
{
	UBYTE b;
	int ones,zeros,len2;

	ones=zeros=0;
	len2=len;
	while(len--) {
		b = (comp[0] << bit) | (comp[1] >> (8 - bit));
		if(b != *src) return 1;
		if(b==0x00) zeros++;
		if(b==0xff) ones++;
		src++;
		comp++;
	}
	if(ones==len2||zeros==len2) return 1;
	return 0;
}

static UBYTE *mergepieces(UBYTE *start,int len,int bits,UBYTE *sync)
{
	UBYTE *dst=tmpmfmbuffer;
	UBYTE b;
	int size;
	int shift;

	size=len-(sync-start);
	memcpy(dst,sync,size);
	dst+=size;
	b=start[len];
	b&=~(255>>bits);
	b|=start[0]>>bits;
	*dst++=b;
	shift=8-bits;
	while(start<=sync+2000) {
		*dst++=(start[0]<<shift)|(start[1]>>(8-shift));
		start++;
	}
	return tmpmfmbuffer;
}

#define SCANOFFSET 1 /* scanning range in bytes, -SCANOFFSET to SCANOFFSET */
#define SCANOFFSET2 20
#define SCANLENGHT 200 /* scanning length in bytes */

static UBYTE* scantrack(UBYTE *sync1,UBYTE *sync2,int *trackbytes,int *trackbits)
{
	int i,bits,bytes,matched;
	UBYTE *sync2bak=sync2;

	sync1+=SCANOFFSET2;
	sync2+=SCANOFFSET2;
	while(sync1 < sync2bak - 2*SCANOFFSET - SCANOFFSET2 - SCANLENGHT) {
		matched=0x7fff;
		for(i=0;i<2*SCANOFFSET*8;i++) {
			bits=i&7;
			bytes=-SCANOFFSET+(i>>3);
			if(!bitshiftcompare(sync1,bits,SCANLENGHT,sync2+bytes)) {
				if(matched==0x7fff) {
					matched=i;
				} else {
					break;
				}
			}
		}
		if(matched!=0x7fff && i>=2*SCANOFFSET*8) {
			bits=matched&7;
			bytes=-SCANOFFSET+(matched>>3);
			*trackbytes=sync2+bytes-sync1;
			*trackbits=bits;
			return mergepieces(sync1,*trackbytes,*trackbits,sync2bak);
		}
		sync1++;
		sync2++;
	}
	return 0;
}
#endif

#define MFMMASK 0x55555555
static ULONG getmfmlong (UWORD * mbuf)
{
	return (ULONG)(((*mbuf << 16) | *(mbuf + 1)) & MFMMASK);
}

#define FLOPPY_WRITE_LEN 6250

static int drive_write_adf_amigados (UWORD *mbuf, UWORD *mend, UBYTE *writebuffer, UBYTE *writebuffer_ok, int track)
{
	int i;
	ULONG odd, even, chksum, id, dlong;
	UBYTE *secdata;
	UBYTE secbuf[544];

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
					printf("* unexpected end of data\n");
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
			printf("* corrupt sector number %d\n", trackoffs);
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
				printf("* sector %d header crc error\n", trackoffs);
				goto next;
			}
			chksum ^= odd ^ even;
		} /* could check here if the label is nonstandard */
		mbuf += 8;
		odd = getmfmlong (mbuf);
		even = getmfmlong (mbuf + 2);
		mbuf += 4;
		if (((odd << 1) | even) != chksum || ((id & 0x00ff0000) >> 16) != (ULONG)track) return 3;
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
			*secdata++ = (UBYTE)(dlong >> 24);
			*secdata++ = (UBYTE)(dlong >> 16);
			*secdata++ = (UBYTE)(dlong >> 8);
			*secdata++ = (UBYTE)dlong;
			chksum ^= odd ^ even;
		}
		mbuf += 256;
		if (chksum) {
			printf("* sector %d data crc error\n", trackoffs);
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
int isamigatrack(UBYTE *mfmdata, int len, UBYTE *writebuffer, UBYTE *writebuffer_ok, int track)
{
	UWORD *dst = amigamfmbuffer;
	int shift, syncshift, sync;
	ULONG l;
	UWORD w;

	len *= 8;
	sync = syncshift = shift = 0;
	while (len--) {
		l = (mfmdata[0] << 16) | (mfmdata[1] << 8) | (mfmdata[2] << 0);
		w = (UWORD)(l >> (8 - shift));
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

#if 0
/* read and examine one raw track, do MFM decoding if track
 * is AmigaDOS formatted and track conversion is not disabled
 */
static int dotrack(int track,UBYTE *buf,FILE *f)
{
	int retries,trackbytes,trackbits,type;
	int out = 0;
	UBYTE *syncpos[10]={0},*trackbuffer,*amigatrack;

	trackbuffer=0;
	for(retries=0;retries<3;retries++) {
		if(!rawread(buf,syncpos,f,track)) return 0;
		trackbytes=trackbits=0;
		trackbuffer=scantrack(syncpos[1],syncpos[2],&trackbytes,&trackbits);
		if(trackbuffer) break;
	}
	if(!trackbuffer) {
		trackbuffer=tmpmfmbuffer;
		memset(trackbuffer,0,LONGEST_TRACK);
	}
	if(!trackbytes) {
		printf("empty track?");
		trackbytes=trackbits=0;
	} else {
		printf("length %d/%d",trackbytes,trackbits);
	}

	type=TRACK_RAW;
	amigatrack=0;

	if(!conversiontable[track]&&trackbytes) {
		amigatrack=isamigatrack(trackbuffer, track);
		if(amigatrack) {
			if(trackbytes<LONG_TRACK) {
				trackbuffer=amigatrack;
				trackbytes=11*512;
				trackbits=0;
				type=TRACK_AMIGADOS;
				printf(" (AmigaDOS)");
				out=1;
			} else {
				printf(" (long AmigaDOS)");
				out=1;
			}
		}
	}
	if(!out) printf (" (raw)");

	tracks[track].trackbits=trackbytes*8+trackbits;
	/* clear unneeded bits from last byte */
	trackbuffer[trackbytes]&=~(255>>(8-trackbits));
	trackbuffer[trackbytes+1]=0;
	if(trackbits) trackbytes++;
	/* track length must be word aligned */
	trackbytes=(trackbytes+1)&~1;
	tracks[track].trackbytes=trackbytes;
	if(trackbytes) fwrite(trackbuffer,tracks[track].trackbytes,1,f);
	tracks[track].type=type;
	return 1;
}

/* create normal (non-raw) ADF file */
static int doamigatrack(int track,UBYTE *buf,FILE *f)
{
	read(track,buf);
	fwrite(buf,amigatracklength,1,f);
	return 1;
}

static void writeword(FILE *f,int w)
{
	UBYTE b;

	b=w>>8;
	fwrite(&b,1,1,f);
	b=w;
	fwrite(&b,1,1,f);
}

static void writelong(FILE *f,int l)
{
	writeword(f,l>>16);
	writeword(f,l);
}

/* update ADF_EXT2 header */
static void fiximage(FILE *f,int numtracks)
{
	int i;

	if(imagetype!=ADF_EXT2) return;
	fseek(f,8+4,SEEK_SET);
	for(i=0;i<numtracks;i++) {
		writelong(f,tracks[i].type);
		writelong(f,tracks[i].trackbytes);
		writelong(f,tracks[i].trackbits);
	}
}

/* create empty ADF_EXT2 */
static void initimage(FILE *f,int numtracks)
{
	if(imagetype!=ADF_EXT2) return;
	fwrite("UAE-1ADF",8,1,f);
	writelong(f,numtracks);
	while(numtracks--) {
		writelong(f,0);
		writelong(f,0);
		writelong(f,0);
		tracks[numtracks].type=TRACK_RAW;
	}
}
#endif
