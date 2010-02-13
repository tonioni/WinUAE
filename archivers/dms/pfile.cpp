
/*
*     xDMS  v1.3  -  Portable DMS archive unpacker  -  Public Domain
*     Written by     Andre Rodrigues de la Rocha  <adlroc@usa.net>
*
*     Handles the processing of a single DMS archive
*
*/


#define HEADLEN 56
#define THLEN 20
#define TRACK_BUFFER_LEN 32000
#define TEMP_BUFFER_LEN 32000


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sysconfig.h"
#include "sysdeps.h"
#include "zfile.h"

#include "cdata.h"
#include "u_init.h"
#include "u_rle.h"
#include "u_quick.h"
#include "u_medium.h"
#include "u_deep.h"
#include "u_heavy.h"
#include "crc_csum.h"
#include "pfile.h"

static int dolog = 0;

#define DMSFLAG_ENCRYPTED 2
#define DMSFLAG_HD 16

static USHORT Process_Track(struct zfile *, struct zfile *, UCHAR *, UCHAR *, USHORT, USHORT, int, struct zfile **extra);
static USHORT Unpack_Track(UCHAR *, UCHAR *, USHORT, USHORT, UCHAR, UCHAR, USHORT, USHORT, USHORT, int);
static void printbandiz(UCHAR *, USHORT);

static int passfound, passretries;

static TCHAR modes[7][7]={L"NOCOMP",L"SIMPLE",L"QUICK ",L"MEDIUM",L"DEEP  ",L"HEAVY1",L"HEAVY2"};
static USHORT PWDCRC;

UCHAR *dms_text;

static void log_error(int track)
{
	write_log (L"DMS: Ignored error on track %d!\n", track);
}

static void addextra(TCHAR *name, struct zfile **extra, uae_u8 *p, int size)
{
	int i;
	struct zfile *zf = NULL;

	if (!extra)
		return;
	for (i = 0; i < DMS_EXTRA_SIZE; i++) {
		if (!extra[i])
			break;
	}
	if (i == DMS_EXTRA_SIZE)
		return;
	zf = zfile_fopen_empty (NULL, name, size);
	if (!zf)
		return;
	zfile_fwrite (p, size, 1, zf);
	zfile_fseek (zf, 0, SEEK_SET);
	extra[i] = zf;
}

USHORT DMS_Process_File(struct zfile *fi, struct zfile *fo, USHORT cmd, USHORT opt, USHORT PCRC, USHORT pwd, int part, struct zfile **extra)
{
	USHORT from, to, geninfo, c_version, cmode, hcrc, disktype, pv, ret;
	ULONG pkfsize, unpkfsize;
	UCHAR *b1, *b2;
	time_t date;

	passfound = 0;
	passretries = 2;
	b1 = xcalloc(UCHAR,TRACK_BUFFER_LEN);
	if (!b1) return ERR_NOMEMORY;
	b2 = xcalloc(UCHAR,TRACK_BUFFER_LEN);
	if (!b2) {
		free(b1);
		return ERR_NOMEMORY;
	}
	dms_text = xcalloc(UCHAR,TEMP_BUFFER_LEN);
	if (!dms_text) {
		free(b1);
		free(b2);
		return ERR_NOMEMORY;
	}

	/* if iname is NULL, input is stdin;   if oname is NULL, output is stdout */

	if (zfile_fread(b1,1,HEADLEN,fi) != HEADLEN) {
		free(b1);
		free(b2);
		free(dms_text);
		return ERR_SREAD;
	}

	if ( (b1[0] != 'D') || (b1[1] != 'M') || (b1[2] != 'S') || (b1[3] != '!') ) {
		/*  Check the first 4 bytes of file to see if it is "DMS!"  */
		free(b1);
		free(b2);
		free(dms_text);
		return ERR_NOTDMS;
	}

	hcrc = (USHORT)((b1[HEADLEN-2]<<8) | b1[HEADLEN-1]);
	/* Header CRC */

	if (hcrc != dms_CreateCRC(b1+4,(ULONG)(HEADLEN-6))) {
		free(b1);
		free(b2);
		free(dms_text);
		return ERR_HCRC;
	}

	geninfo = (USHORT) ((b1[10]<<8) | b1[11]);	/* General info about archive */
	date = (time_t) ((((ULONG)b1[12])<<24) | (((ULONG)b1[13])<<16) | (((ULONG)b1[14])<<8) | (ULONG)b1[15]);	/* date in standard UNIX/ANSI format */
	from = (USHORT) ((b1[16]<<8) | b1[17]);		/*  Lowest track in archive. May be incorrect if archive is "appended" */
	to = (USHORT) ((b1[18]<<8) | b1[19]);		/*  Highest track in archive. May be incorrect if archive is "appended" */

	if (part && from < 30) {
		free(b1);
		free(b2);
		free(dms_text);
		return DMS_FILE_END;
	}

	pkfsize = (ULONG) ((((ULONG)b1[21])<<16) | (((ULONG)b1[22])<<8) | (ULONG)b1[23]);	/*  Length of total packed data as in archive   */
	unpkfsize = (ULONG) ((((ULONG)b1[25])<<16) | (((ULONG)b1[26])<<8) | (ULONG)b1[27]);	/*  Length of unpacked data. Usually 901120 bytes  */

	c_version = (USHORT) ((b1[46]<<8) | b1[47]);	/*  version of DMS used to generate it  */
	disktype = (USHORT) ((b1[50]<<8) | b1[51]);		/*  Type of compressed disk  */
	cmode = (USHORT) ((b1[52]<<8) | b1[53]);        /*  Compression mode mostly used in this archive  */

	PWDCRC = PCRC;

	if (dolog) {

		pv = (USHORT)(c_version/100);
		write_log (L" Created with DMS version %d.%02d ",pv,c_version-pv*100);
		if (geninfo & 0x80)
			write_log (L"Registered\n");
		else
			write_log (L"Evaluation\n");

		write_log (L" Creation date : %s",ctime(&date));
		write_log (L" Lowest track in archive : %d\n",from);
		write_log (L" Highest track in archive : %d\n",to);
		write_log (L" Packed data size : %lu\n",pkfsize);
		write_log (L" Unpacked data size : %lu\n",unpkfsize);
		write_log (L" Disk type of archive : ");

		/*  The original DMS from SDS software (DMS up to 1.11) used other values    */
		/*  in disk type to indicate formats as MS-DOS, AMax and Mac, but it was     */
		/*  not suported for compression. It was for future expansion and was never  */
		/*  used. The newer versions of DMS made by ParCon Software changed it to    */
		/*  add support for new Amiga disk types.                                    */
		switch (disktype) {
			case 0:
			case 1:
				/* Can also be a non-dos disk */
				write_log (L"AmigaOS 1.0 OFS\n");
				break;
			case 2:
				write_log (L"AmigaOS 2.0 FFS\n");
				break;
			case 3:
				write_log (L"AmigaOS 3.0 OFS / International\n");
				break;
			case 4:
				write_log (L"AmigaOS 3.0 FFS / International\n");
				break;
			case 5:
				write_log (L"AmigaOS 3.0 OFS / Dir Cache\n");
				break;
			case 6:
				write_log (L"AmigaOS 3.0 FFS / Dir Cache\n");
				break;
			case 7:
				write_log (L"FMS Amiga System File\n");
				break;
			default:
				write_log (L"Unknown\n");
		}

		write_log (L" Compression mode used : ");
		if (cmode>6)
			write_log (L"Unknown !\n");
		else
			write_log (L"%s\n",modes[cmode]);

		write_log (L" General info : ");
		if ((geninfo==0)||(geninfo==0x80)) write_log (L"None");
		if (geninfo & 1) write_log (L"NoZero ");
		if (geninfo & 2) write_log (L"Encrypted ");
		if (geninfo & 4) write_log (L"Appends ");
		if (geninfo & 8) write_log (L"Banner ");
		if (geninfo & 16) write_log (L"HD ");
		if (geninfo & 32) write_log (L"MS-DOS ");
		if (geninfo & 64) write_log (L"DMS_DEV_Fixed ");
		if (geninfo & 256) write_log (L"FILEID.DIZ");
		write_log (L"\n");

		write_log (L" Info Header CRC : %04X\n\n",hcrc);

	}

	if (disktype == 7) {
		/*  It's not a DMS compressed disk image, but a FMS archive  */
		free(b1);
		free(b2);
		free(dms_text);
		return ERR_FMS;
	}


	if (dolog)	{
		write_log (L" Track   Plength  Ulength  Cmode   USUM  HCRC  DCRC Cflag\n");
		write_log (L" ------  -------  -------  ------  ----  ----  ---- -----\n");
	}

	//	if (((cmd==CMD_UNPACK) || (cmd==CMD_SHOWBANNER)) && (geninfo & 2) && (!pwd))
	//		return ERR_NOPASSWD;

	ret=NO_PROBLEM;

	Init_Decrunchers();

	if (cmd != CMD_VIEW) {
		if (cmd == CMD_SHOWBANNER) /*  Banner is in the first track  */
			ret = Process_Track(fi,NULL,b1,b2,cmd,opt,geninfo,extra);
		else {
			for (;;) {
				int ok = 0;
				ret = Process_Track(fi,fo,b1,b2,cmd,opt,geninfo,extra);
				if (ret == DMS_FILE_END)
					break;
				if (ret == NO_PROBLEM)
					continue;
				break;
#if 0
				while (!ok) {
					uae_u8 b1[THLEN];

					if (zfile_fread(b1,1,THLEN,fi) != 1) {
						write_log (L"DMS: unexpected end of file\n");
						break;
					}
					write_log (L"DMS: corrupted track, searching for next track header..\n");
					if (b1[0] == 'T' && b1[1] == 'R') {
						USHORT hcrc = (USHORT)((b1[THLEN-2] << 8) | b1[THLEN-1]);
						if (CreateCRC(b1,(ULONG)(THLEN-2)) == hcrc) {
							write_log (L"DMS: found checksum correct track header, retrying..\n");
							zfile_fseek (fi, SEEK_CUR, -THLEN);
							ok = 1;
							break;
						}
					}
					if (!ok)
						zfile_fseek (fi, SEEK_CUR, -(THLEN - 1));
				}
#endif
			}
		}
	}

	if ((cmd == CMD_VIEWFULL) || (cmd == CMD_SHOWDIZ) || (cmd == CMD_SHOWBANNER)) write_log (L"\n");

	if (ret == DMS_FILE_END) ret = NO_PROBLEM;


	/*  Used to give an error message, but I have seen some DMS  */
	/*  files with texts or zeros at the end of the valid data   */
	/*  So, when we find something that is not a track header,   */
	/*  we suppose that the valid data is over. And say it's ok. */
	if (ret == ERR_NOTTRACK) ret = NO_PROBLEM;


	free(b1);
	free(b2);
	free(dms_text);

	return ret;
}

static USHORT Process_Track(struct zfile *fi, struct zfile *fo, UCHAR *b1, UCHAR *b2, USHORT cmd, USHORT opt, int dmsflags, struct zfile **extra){
	USHORT hcrc, dcrc, usum, number, pklen1, pklen2, unpklen, l;
	UCHAR cmode, flags;
	int crcerr = 0;


	l = (USHORT)zfile_fread(b1,1,THLEN,fi);

	if (l != THLEN) {
		if (l==0)
			return DMS_FILE_END;
		else
			return ERR_SREAD;
	}

	/*  "TR" identifies a Track Header  */
	if ((b1[0] != 'T')||(b1[1] != 'R')) return ERR_NOTTRACK;

	/*  Track Header CRC  */
	hcrc = (USHORT)((b1[THLEN-2] << 8) | b1[THLEN-1]);

	if (dms_CreateCRC(b1,(ULONG)(THLEN-2)) != hcrc) return ERR_THCRC;

	number = (USHORT)((b1[2] << 8) | b1[3]);	/*  Number of track  */
	pklen1 = (USHORT)((b1[6] << 8) | b1[7]);	/*  Length of packed track data as in archive  */
	pklen2 = (USHORT)((b1[8] << 8) | b1[9]);	/*  Length of data after first unpacking  */
	unpklen = (USHORT)((b1[10] << 8) | b1[11]);	/*  Length of data after subsequent rle unpacking */
	flags = b1[12];		/*  control flags  */
	cmode = b1[13];		/*  compression mode used  */
	usum = (USHORT)((b1[14] << 8) | b1[15]);	/*  Track Data CheckSum AFTER unpacking  */
	dcrc = (USHORT)((b1[16] << 8) | b1[17]);	/*  Track Data CRC BEFORE unpacking  */

	if (dolog)
		write_log (L"DMS: track=%d\n", number);

	if (dolog) {
		if (number==80)
			write_log (L" FileID   ");
		else if (number==0xffff)
			write_log (L" Banner   ");
		else if ((number==0) && (unpklen==1024))
			write_log (L" FakeBB   ");
		else
			write_log (L"   %2d     ",(short)number);

		write_log (L"%5d    %5d   %s  %04X  %04X  %04X    %0d\n", pklen1, unpklen, modes[cmode], usum, hcrc, dcrc, flags);
	}

	if ((pklen1 > TRACK_BUFFER_LEN) || (pklen2 >TRACK_BUFFER_LEN) || (unpklen > TRACK_BUFFER_LEN)) return ERR_BIGTRACK;

	if (zfile_fread(b1,1,(size_t)pklen1,fi) != pklen1) return ERR_SREAD;

	if (dms_CreateCRC(b1,(ULONG)pklen1) != dcrc) {
		log_error (number);
		crcerr = 1;
	}
	/*  track 80 is FILEID.DIZ, track 0xffff (-1) is Banner  */
	/*  and track 0 with 1024 bytes only is a fake boot block with more advertising */
	/*  FILE_ID.DIZ is never encrypted  */

	//if (pwd && (number!=80)) dms_decrypt(b1,pklen1);

	if ((cmd == CMD_UNPACK) && (number<80) && (unpklen>2048)) {
		memset(b2, 0, unpklen);
		if (!crcerr)
			Unpack_Track(b1, b2, pklen2, unpklen, cmode, flags, number, pklen1, usum, dmsflags & DMSFLAG_ENCRYPTED);
		if (number == 0 && zfile_ftell (fo) == 512 * 22) {
			// did we have another cylinder 0 already?
			uae_u8 *p;
			zfile_fseek (fo, 0, SEEK_SET);
			p = xcalloc (uae_u8, 512 * 22);
			zfile_fread (p, 512 * 22, 1, fo);
			addextra(L"BigFakeBootBlock", extra, p, 512 * 22);
			xfree (p);
		}
		zfile_fseek (fo, number * 512 * 22 * ((dmsflags & DMSFLAG_HD) ? 2 : 1), SEEK_SET);
		if (zfile_fwrite(b2,1,(size_t)unpklen,fo) != unpklen)
			return ERR_CANTWRITE;
	} else if (number == 0 && unpklen == 1024) {
		memset(b2, 0, unpklen);
		if (!crcerr)
			Unpack_Track(b1, b2, pklen2, unpklen, cmode, flags, number, pklen1, usum, dmsflags & DMSFLAG_ENCRYPTED);
		addextra(L"FakeBootBlock", extra, b2, unpklen);
	}

	if (crcerr)
		return NO_PROBLEM;

	if (number == 0xffff && extra){
		Unpack_Track(b1, b2, pklen2, unpklen, cmode, flags, number, pklen1, usum, dmsflags & DMSFLAG_ENCRYPTED);
		addextra(L"Banner", extra, b2, unpklen);
		//printbandiz(b2,unpklen);
	}

	if (number == 80 && extra) {
		Unpack_Track(b1, b2, pklen2, unpklen, cmode, flags, number, pklen1, usum, dmsflags & DMSFLAG_ENCRYPTED);
		addextra(L"FILEID.DIZ", extra, b2, unpklen);
		//printbandiz(b2,unpklen);
	}

	return NO_PROBLEM;

}



static USHORT Unpack_Track_2(UCHAR *b1, UCHAR *b2, USHORT pklen2, USHORT unpklen, UCHAR cmode, UCHAR flags){
	switch (cmode){
		case 0:
			/*   No Compression   */
			memcpy(b2,b1,(size_t)unpklen);
			break;
		case 1:
			/*   Simple Compression   */
			if (Unpack_RLE(b1,b2,unpklen)) return ERR_BADDECR;
			break;
		case 2:
			/*   Quick Compression   */
			if (Unpack_QUICK(b1,b2,pklen2)) return ERR_BADDECR;
			if (Unpack_RLE(b2,b1,unpklen)) return ERR_BADDECR;
			memcpy(b2,b1,(size_t)unpklen);
			break;
		case 3:
			/*   Medium Compression   */
			if (Unpack_MEDIUM(b1,b2,pklen2)) return ERR_BADDECR;
			if (Unpack_RLE(b2,b1,unpklen)) return ERR_BADDECR;
			memcpy(b2,b1,(size_t)unpklen);
			break;
		case 4:
			/*   Deep Compression   */
			if (Unpack_DEEP(b1,b2,pklen2)) return ERR_BADDECR;
			if (Unpack_RLE(b2,b1,unpklen)) return ERR_BADDECR;
			memcpy(b2,b1,(size_t)unpklen);
			break;
		case 5:
		case 6:
			/*   Heavy Compression   */
			if (cmode==5) {
				/*   Heavy 1   */
				if (Unpack_HEAVY(b1,b2,flags & 7,pklen2)) return ERR_BADDECR;
			} else {
				/*   Heavy 2   */
				if (Unpack_HEAVY(b1,b2,flags | 8,pklen2)) return ERR_BADDECR;
			}
			if (flags & 4) {
				/*  Unpack with RLE only if this flag is set  */
				if (Unpack_RLE(b2,b1,unpklen)) return ERR_BADDECR;
				memcpy(b2,b1,(size_t)unpklen);
			}
			break;
		default:
			return ERR_UNKNMODE;
	}

	if (!(flags & 1)) Init_Decrunchers();

	return NO_PROBLEM;

}

/*  DMS uses a lame encryption  */
static void dms_decrypt(UCHAR *p, USHORT len, UCHAR *src){
	USHORT t;

	while (len--){
		t = (USHORT) *src++;
		*p++ = t ^ (UCHAR)PWDCRC;
		PWDCRC = (USHORT)((PWDCRC >> 1) + t);
	}
}

static USHORT Unpack_Track(UCHAR *b1, UCHAR *b2, USHORT pklen2, USHORT unpklen, UCHAR cmode, UCHAR flags, USHORT number, USHORT pklen1, USHORT usum1, int enc)
{
	USHORT r, err = NO_PROBLEM;
	static USHORT pass;
	int maybeencrypted;
	int pwrounds;
	UCHAR *tmp;
	USHORT prevpass = 0;

	if (passfound) {
		if (number != 80)
			dms_decrypt(b1, pklen1, b1);
		r = Unpack_Track_2(b1, b2, pklen2, unpklen, cmode, flags);
		if (r == NO_PROBLEM) {
			if (usum1 == dms_Calc_CheckSum(b2,(ULONG)unpklen))
				return NO_PROBLEM;
		}
		log_error(number);
		if (passretries <= 0)
			return ERR_CSUM;
	}

	passretries--;
	pwrounds = 0;
	maybeencrypted = 0;
	tmp = (unsigned char*)malloc (pklen1);
	memcpy (tmp, b1, pklen1);
	memset(b2, 0, unpklen);
	for (;;) {
		r = Unpack_Track_2(b1, b2, pklen2, unpklen, cmode, flags);
		if (r == NO_PROBLEM) {
			if (usum1 == dms_Calc_CheckSum(b2,(ULONG)unpklen)) {
				passfound = maybeencrypted;
				if (passfound)
					write_log (L"DMS: decryption key = 0x%04X\n", prevpass);
				err = NO_PROBLEM;
				pass = prevpass;
				break;
			}
		}
		if (number == 80 || !enc) {
			err = ERR_CSUM;
			break;
		}
		maybeencrypted = 1;
		prevpass = pass;
		PWDCRC = pass;
		pass++;
		dms_decrypt(b1, pklen1, tmp);
		pwrounds++;
		if (pwrounds == 65536) {
			err = ERR_CSUM;
			passfound = 0;
			break;
		}
	}
	free (tmp);
	return err;
}


static void printbandiz(UCHAR *m, USHORT len){
	UCHAR *i,*j;

	i=j=m;
	while (i<m+len) {
		if (*i == 10) {
			TCHAR *u;
			*i=0;
			u = au ((char*)j);
			write_log (L"%s\n",u);
			xfree (u);
			j=i+1;
		}
		i++;
	}
}


