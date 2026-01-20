/* 22 mar 2003
 * GnuPlayer.c
 * based on XtC's description ! ... good job ! :).
 * 
*/
/* testGnuPlayer() */
/* Rip_GnuPlayer() */
/* Depack_GnuPlayer() */

#include "globals.h"
#include "extern.h"


short testGnuPlayer ( void )
{
  /* test #1 */
  if ( PW_i < 0x92 )
  {
    /*printf ( "#0 (start:%ld) \n", PW_i - 0x92 );*/
    return BAD;
  }

  /* test #2 smp size and loop start */
  PW_WholeSampleSize = 0;
  PW_o = 0;  /* will hold the number of non-null samples */
  PW_Start_Address = PW_i - 0x92;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+20+PW_k*4]*256) + in_data[PW_Start_Address+21+PW_k*4];
    PW_l = (in_data[PW_Start_Address+22+PW_k*4]*256) + in_data[PW_Start_Address+23+PW_k*4];
    if ( PW_l > (PW_j+1) )
    {
      /*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_j != 0 ) PW_o += 1;
    PW_WholeSampleSize += PW_j;
  }

  return GOOD;
}



void Rip_GnuPlayer ( void )
{
  /*PW_WholeSampleSize is the whole sample size */
  /*but it seems to be a fake sample values */
  
  /* size of 1st track + header */
  PW_k = (in_data[PW_Start_Address + 0x96]*256)+in_data[PW_Start_Address + 0x97] + 0x96;
  /* size of 2nd track */
  PW_j = (in_data[PW_k]*256) + in_data[PW_k+1] + PW_k;
  /* PW_j points now to the first sample size */
  /*printf ( "\nWhere before 1st sample : %ld\n" , PW_j);*/

  /* real sample sizes */
  PW_m = 0;
  while ( PW_m < PW_o )
  {
    PW_k = (in_data[PW_Start_Address+PW_j]*256) + in_data[PW_Start_Address+PW_j+1];
    PW_j += (PW_k + 2);
    PW_m += 1;
    /*printf ( "sample %ld : siz:%ld where:%ld\n", PW_m,PW_k,PW_j );
      fflush (stdout);*/
  }

  OutputSize = PW_j;

  CONVERT = GOOD;
  Save_Rip ( "GnuPlayer module", GnuPlayer );
  
  if ( Save_Status == GOOD )
    PW_i += 0x96;
}

/*
 *   GnuPlayer.c   2003 (c) Asle@free.fr
 *
*/


#define MAXI(a,b) (a>b?a:b)

void Depack_GnuPlayer ( void )
{
  Uchar *Whatever;
  long i=0,j=0,k=0,l=0;
  long Where = PW_Start_Address;
  Uchar * Pattern;
  Uchar poss[37][2];
  FILE *out;/*,*info;*/
  long SizOfTrack,len1,len2;
  long SmpSizes[31];
  long NbrSmp = 0;
  short SfxNbr=0;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b" );*/

  /* read and write title */
  fwrite ( &in_data[Where] , 20 , 1 , out );

  /* get global sample rate */
  /*
  for ( SmpRate=0x00 ; i<37 ; SmpRate+=0x01 )
    if ( (poss[SmpRate][0] == in_data[Where + 0x90]) && (poss[SmpRate][1] == in_data[Where + 0x91]))
      break;
  */

  /*take care of pattern right now*/
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  Where = PW_Start_Address + 0x96;
  /* track 1 & 2 */
  SizOfTrack = (in_data[Where]*256) + in_data[Where+1]; /* size of track 1 */
  /*fprintf ( info, "Size of track 1 : %ld (%x)\n", SizOfTrack,SizOfTrack );*/
  Where += 2; len1=0;l=0;
  for ( i=0 ; i<SizOfTrack ; i+=2 )
  {
    switch (in_data[Where+i])
    {
      case 0: /* track end */
	/*fprintf ( info, "[%3ld][%4ld] <-- end of track\n\n", i,l );*/
	i = SizOfTrack;
	break;
      case 1: /* set volume */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] C fx (arg:%2d) [sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	Pattern[l+2] |= 0x0C;
	Pattern[l+3] = in_data[Where+1+i];
	Pattern[l+6] |= 0x0C;
	Pattern[l+7] = in_data[Where+1+i];
	break;
      case 2: /* same a A */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] A fx (arg:%2d) [sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	Pattern[l+2] += 0x0A;
	Pattern[l+3] = in_data[Where+1+i];
	Pattern[l+6] += 0x0A;
	Pattern[l+7] = in_data[Where+1+i];
	break;
      case 3: /* set speed */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] F fx (arg:%2d) [sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	if ( SfxNbr == 1 )
	{
	  Pattern[l+2] |= 0x0f;
	  Pattern[l+3] = in_data[Where+1+i];
	}
	else /* if SfxNbr == 3, I'm in trouble :( */
	{
	  Pattern[l+6] |= 0x0f;
	  Pattern[l+7] = in_data[Where+1+i];
	}
	break;
      case 4: /* bypass rows */
	/*fprintf ( info, "[%3ld][%4ld] bypass rows : %d\n",i,l,in_data[Where+1+i]);*/
	l += (in_data[Where+i+1] * 16);
	SfxNbr = 0;
	break;
      case 5: /* set note */
	/*fprintf ( info, "[%3ld][%4ld] set note with smp nbr %d\n",i,l,in_data[Where+1+i]);*/
	Pattern[l] = (in_data[Where+1+i]&0xf0);
	Pattern[l] |= in_data[PW_Start_Address + 0x90];
	Pattern[l+1] = in_data[PW_Start_Address + 0x91];
	Pattern[l+4] = (in_data[Where+1+i]&0xf0);
	Pattern[l+4] |= in_data[PW_Start_Address + 0x90];
	Pattern[l+5] = in_data[PW_Start_Address + 0x91];
	Pattern[l+2] |= (in_data[Where+1+i]<<4);
	Pattern[l+6] |= (in_data[Where+1+i]<<4);
	break;
      default :
	printf ( "\nunsupported case in Depack_GnuPlayer(). Please send this file to \"asle@free.fr\" :)\n" );
	break;
    }
  }
  len1 = l/1024;

  /* track 3 & 4 */
  Where += (SizOfTrack - 2);
  SizOfTrack = (in_data[Where]*256) + in_data[Where+1]; /* size of track 2 */
  /*fprintf ( info, "Size of track 2 : %ld (%x)\n", SizOfTrack,SizOfTrack );*/
  Where += 2; len2=0;l=0;
  for ( i=0 ; i<SizOfTrack ; i+=2 )
  {
    switch (in_data[Where+i])
    {
      case 0: /* track end */
	/*fprintf ( info, "[%3ld][%4ld] <-- end of track\n\n", i,l );*/
	i = SizOfTrack;
	break;
      case 1: /* set volume */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] C fx (arg:%d) [Sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	Pattern[l+10] |= 0x0C;
	Pattern[l+11] = in_data[Where+1+i];
	Pattern[l+14] |= 0x0C;
	Pattern[l+15] = in_data[Where+1+i];
	break;
      case 2: /* same a A */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] A fx (arg:%2d) [Sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	Pattern[l+10] |= 0x0A;
	Pattern[l+11] = in_data[Where+1+i];
	Pattern[l+14] |= 0x0A;
	Pattern[l+15] = in_data[Where+1+i];
	break;
      case 3: /* set speed */
	SfxNbr += 1;
	/*fprintf ( info, "[%3ld][%4ld] F fx (arg:%2d) [Sfx %d]\n",i,l,in_data[Where+1+i],SfxNbr);*/
	if ( SfxNbr == 1 )
	{
	  Pattern[l+10] |= 0x0f;
	  Pattern[l+11] = in_data[Where+1+i];
	}
	else
	{
	  Pattern[l+14] |= 0x0f;
	  Pattern[l+15] = in_data[Where+1+i];
	}
	break;
      case 4: /* bypass rows */
	/*fprintf ( info, "[%3ld][%4ld] bypass rows : %d\n",i,l,in_data[Where+1+i]);*/
	l += (in_data[Where+i+1] * 16);
	SfxNbr = 0;
	break;
      case 5: /* set note */
	/*fprintf ( info, "[%3ld][%4ld] set note with smp nbr %d\n",i,l,in_data[Where+1+i]);*/
	Pattern[l+8] = (in_data[Where+1+i]&0xf0);
	Pattern[l+8] |= in_data[PW_Start_Address + 0x90];
	Pattern[l+9] = in_data[PW_Start_Address + 0x91];
	Pattern[l+12] = (in_data[Where+1+i]&0xf0);
	Pattern[l+12] |= in_data[PW_Start_Address + 0x90];
	Pattern[l+13] = in_data[PW_Start_Address + 0x91];
	Pattern[l+10] |= (in_data[Where+1+i]<<4);
	Pattern[l+14] |= (in_data[Where+1+i]<<4);
	break;
      default :
	printf ( "\nunsupported case in Depack_GnuPlayer(). Please send this file to \"asle@free.fr\" :)\n" );
	break;
    }
  }
  len2 = l/1024;
  Where += (SizOfTrack - 2);
  /*fprintf ( info, "\nWhere before first sample : %ld (%x)\n", Where,Where );*/

  /* sample header stuff */
  Whatever = (Uchar *) malloc ( 2048 );
  BZERO (Whatever, 2048);
  /*get nbr of non-null samples */
  for ( i=0 ; i< 31 ; i++)
  {
    k = (in_data[PW_Start_Address + 20 + (i*4)]*256) + in_data[PW_Start_Address + 21 + (i*4)];
    if ( k != 0 )
      NbrSmp += 1;
    SmpSizes[i] = k;

    Whatever[22+(i*30)] = in_data[PW_Start_Address + 20 + (i*4)];
    Whatever[23+(i*30)] = in_data[PW_Start_Address + 21 + (i*4)];
    Whatever[25+(i*30)] = 0x40;
    Whatever[26+(i*30)] = in_data[PW_Start_Address + 22 + (i*4)];
    Whatever[27+(i*30)] = in_data[PW_Start_Address + 23 + (i*4)];
    Whatever[29+(i*30)] = 0x01;
  }
  k = MAXI(len1,len2);
  Whatever[930] = k;
  Whatever[931] = 0x7f;
  for ( i=0; i<k ;i++ )
    Whatever[i+932] = (Uchar) i;
  Whatever[1060] = 'M';
  Whatever[1061] = '.';
  Whatever[1062] = 'K';
  Whatever[1063] = '.';
  fwrite ( Whatever, 1064, 1, out );

  /* write patterns */
  /* k being the max of both lengths of tracks */
  fwrite ( Pattern, k*1024 , 1 , out );
  free ( Pattern );

  /* sample stuff */
  free ( Whatever );
  Whatever = (Uchar *) malloc (65536);
  for ( i=0 ; i<NbrSmp ; i++ )
  {
    long out_end;
    char samp;
    k = 0;
    BZERO (Whatever,65536);
    j = (in_data[Where]*256) + in_data[Where+1];
    Where += 2;
    out_end = (j*2);
    /*fprintf ( info, "sample %ld : siz:%ld where:%ld\n" , i,out_end,Where);*/
    /*fflush ( info );*/
    Whatever[k++] = in_data[Where++];
    while ( k < out_end )
    {
      samp = (in_data[Where]>>4)&0x0f;
      if ( samp & 0x08 ) samp -= 0x10;
      Whatever[k++] = (Whatever[k-1] + samp);
      samp = in_data[Where] & 0x0f;
      if ( samp & 0x08 ) samp -= 0x10;
      Whatever[k++] = (Whatever[k-1] + samp);
      Where += 1;
    }
    Where -= 1;
    fwrite ( &Whatever[0], out_end, 1, out );
  }
  free ( Whatever );

  /* crap */
  Crap ( "    GnuPlayer     " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*fclose ( info );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
