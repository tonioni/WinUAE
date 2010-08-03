/* testSKYT() */
/* Rip_SKYT() */
/* Depack_SKYT() */


/*
 * Even in these poor lines, I managed to insert a bug :)
 * 
 * update: 19/04/00
 *  - bug correction (really testing volume !)
*/

#include "globals.h"
#include "extern.h"


short testSKYT ( void )
{
  /* test 1 */
  if ( PW_i < 256 )
  {
    return BAD;
  }

  /* test 2 */
  PW_WholeSampleSize = 0;
  PW_Start_Address = PW_i-256;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+3+8*PW_j] > 0x40 )
    {
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+8*PW_j]*256)+in_data[PW_Start_Address+1+8*PW_j])*2);
  }

  return GOOD;
}



void Rip_SKYT ( void )
{
  /* PW_WholeSampleSize is still the whole sample size */

  PW_l=0;
  PW_j = in_data[PW_Start_Address+260]+1;
  for ( PW_k=0 ; PW_k<(PW_j*4) ; PW_k++ )
  {
    PW_m = (in_data[PW_Start_Address+262+PW_k*2]);
    if ( PW_m > PW_l )
    {
      PW_l = PW_m;
    }
    /*printf ( "[%ld]:%ld\n",PW_k,PW_m);*/
  }
  OutputSize = (PW_l*256) + 262 + PW_WholeSampleSize + (PW_j*8);

  CONVERT = GOOD;
  Save_Rip ( "SKYT Packed module", SKYT_packer );
  
  if ( Save_Status == GOOD )
    PW_i += 257;
}



/*
 *   Skyt_Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk SKYT packed MODs
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 Nov 1999 : Update
 *   - Speed & Size optimizings
 * 19 Apr 2000 : Update
 *   - address of samples data bug correction
 *     (thx to Thomas Neumann)
 * 29 Nov 2003 : Update
 *   - another bug removed :(.
 * 2 Aug 2010 : 
 *   - cleaned up a bit
 *   - track 0 was not correctly handled
 * 3 Aug 2010 : 
 *   - rewrote patternlist generation
*/

void Depack_SKYT ( void )
{
  Uchar *Header, *Pattern;
  Uchar poss[37][2];
  long i=0,j=0,k=0,l=0,m=0;
  long Total_Sample_Size=0;
  unsigned long ReadTrkPat[128][4], ReadPat[128];
  long Where=PW_Start_Address;
  long Highest_Track = 0;
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Header = (Uchar *)malloc(1084);
  Pattern = (Uchar *)malloc(1024);
  BZERO ( Header , 1084 );
  BZERO ( Pattern , 1024 );

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    /* write Size,Fine,Vol & Loop start */
    Header[42+i*30] = in_data[Where];
    Header[43+i*30] = in_data[Where+1];
    Header[44+i*30] = in_data[Where+2];
    Header[45+i*30] = in_data[Where+3];
    Header[46+i*30] = in_data[Where+4];
    Header[47+i*30] = in_data[Where+5];
    Header[48+i*30] = in_data[Where+6];
    Header[49+i*30] = in_data[Where+7];
    /* loop size */
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )
      Header[49+i*30] = 0x01;
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* go to track addresses list */
  Where = PW_Start_Address + 260;

  /* pattern table lenght */
  Header[950] = in_data[Where]+1;
  Where += 1;
  /*printf ( "Size of pattern list : %d\n" , Header[950] );*/

  /* write NoiseTracker byte */
  Header[951] = 0x7F;

  /* pattern list */
  /* those are tracks, being saved. Pattern list must be regenerated */
  /* each track is 256 bytes (4*64 notes) */
  for (i=0;i<Header[950];i++)
  {
    ReadPat[i] = (in_data[Where+i*8+1]*256*256*256) + 
      (in_data[Where+3+i*8]*256*256) +
      (in_data[Where+5+i*8]*256) +
      in_data[Where+7+i*8];
    ReadTrkPat[i][0] = in_data[Where+i*8+1]-1;
    ReadTrkPat[i][1] = in_data[Where+i*8+3]-1;
    ReadTrkPat[i][2] = in_data[Where+i*8+5]-1;
    ReadTrkPat[i][3] = in_data[Where+i*8+7]-1;
    if ((ReadTrkPat[i][0] != 0xffffffff) && (ReadTrkPat[i][0] > Highest_Track))
      Highest_Track = ReadTrkPat[i][0];
    if ((ReadTrkPat[i][1] != 0xffffffff) && (ReadTrkPat[i][1] > Highest_Track))
      Highest_Track = ReadTrkPat[i][1];
    if ((ReadTrkPat[i][2] != 0xffffffff) && (ReadTrkPat[i][2] > Highest_Track))
      Highest_Track = ReadTrkPat[i][2];
    if ((ReadTrkPat[i][3] != 0xffffffff) && (ReadTrkPat[i][3] > Highest_Track))
      Highest_Track = ReadTrkPat[i][3];
    /*printf ("%x-%x-%x-%x\n",ReadTrkPat[i][0],ReadTrkPat[i][1],ReadTrkPat[i][2],ReadTrkPat[i][3]);*/
  }
  /*printf ("highest track nbr : %lx\n", Highest_Track);*/
  
  /* sorting ?*/
  k = 0; /* next min */
  l = 0;

  /* put the first pattern number */
  /* could be stored several times */
  for (j=0; j<Header[950] ; j++)
  {
    m = 0x7fffffff; /* min */
    /*search for min */
    for (i=0; i<Header[950] ; i++)
      if ((ReadPat[i]<m) && (ReadPat[i]>k))
	    m = ReadPat[i];
    /* if k == m then an already existing ref was found */
    if (k==m)
      continue;
    /* m is the next minimum */
    k = m;
    for (i=0; i<Header[950] ; i++)
      if (ReadPat[i] == k)
	Header[952+i] = (unsigned char)l;
    l++;
  }

  /* write ptk's ID */
  Header[1080] = 'M';
  Header[1081] = Header[1083] = '.';
  Header[1082] = 'K';
  fwrite (Header, 1084, 1, out);

  /* get track address */
  Where = PW_Start_Address + 261 + (Header[950]*8) + 1;
  /*printf ("Address of 1st track : %lx\n",Where);*/

  /* l is the number of stored patterns */
  /* rebuild pattern data now */
  for (i=0;i<l-1;i++)
  {
    long min=50,max=0;
    BZERO(Pattern,1024);
    /* which pattern is it now ? */
    for (j=0;j<Header[950];j++)
    {
      if (Header[952+j] == i)
        break; /* found */
    }
    for (k=0;k<4;k++) /* loop on 4 tracks' refs*/
    {
      long d;

      /* empty track */
      if (ReadTrkPat[j][k] == 0xffffffff)
        continue;

      /* loop on notes */
      for (d=0;d<64;d++)
      {
        unsigned char note = in_data[Where+(ReadTrkPat[j][k]*256)+d*4];
        /*if (note > 37)
          printf ("\nbad note '%x' at %lx (ptk:%lx)(pattern:%ld)(voice:%ld)(row:%ld)(track:%lx)",
          note, Where+(ReadTrkPat[j][k]*256)+d*4, k*4+d*16, i,k,d,ReadTrkPat[j][k]);*/
	    Pattern[k*4+d*16] = poss[note][0];
        Pattern[k*4+d*16+1] = poss[note][1];
	    /* samples */
        Pattern[k*4+d*16] |= (in_data[Where+(ReadTrkPat[j][k]*256)+d*4+1])&0xf0;
        Pattern[k*4+d*16+2] = (in_data[Where+(ReadTrkPat[j][k]*256)+d*4+1])<<4;
        Pattern[k*4+d*16+2] += in_data[Where+(ReadTrkPat[j][k]*256)+d*4+2];
        Pattern[k*4+d*16+3] = in_data[Where+(ReadTrkPat[j][k]*256)+d*4+3];
      }
    }
    fwrite ( Pattern , 1024 , 1 , out );
  }
  free ( Pattern );
  free ( Header );

  /* sample data */
  Where += (Highest_Track+1)*256;
  /*printf ("address of sample data : %ld\n",Where);*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  /* crap */
  Crap ( "   SKYT Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
