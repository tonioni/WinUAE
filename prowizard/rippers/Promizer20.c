/* testPM2() */
/* Rip_PM20() */
/* Depack_PM20() */

#include "globals.h"
#include "extern.h"


short testPM2 ( void )
{
  PW_Start_Address = PW_i;
  /* test 1 */
  if ( (PW_Start_Address + 5714) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  /*if ( in_data[PW_Start_Address+5094] != 0x03 )*/
    /* not sure in fact ... */
    /* well, it IS the frequency table, it always seem */
    /* to be the 'standard one .. so here, there is 0358h */
  /*  {
    return BAD;
    }*/
  
  /* test 3 */
  if ( in_data[PW_Start_Address+5461] > 0x40 )
    /* testing a volume */
  {
    return BAD;
  }

  return GOOD;
}



void Rip_PM20 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+5458+PW_k*8]*256)+in_data[PW_Start_Address+5459+PW_k*8])*2);
  PW_j = (in_data[PW_Start_Address+5706]*256*256*256)+(in_data[PW_Start_Address+5707]*256*256)+(in_data[PW_Start_Address+5708]*256)+in_data[PW_Start_Address+5709];
  OutputSize = PW_WholeSampleSize + 5198 + PW_j;

  CONVERT = GOOD;
  Save_Rip ( "Promizer 2.0 module", Promizer_20 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}



/*
 *   Promizer_20.c   1997 (c) Asle / ReDoX
 *
 * Converts PM20 packed MODs back to PTK MODs
 *
 * update 20 mar 2003 (it's war time again .. brrrr)
 * - removed all open() funcs.
 * - optimized more than quite a bit (src 4kb shorter !)
*/

#define ON  0
#define OFF 1
#define AFTER_REPLAY_CODE   5198
#define SAMPLE_DESC         5458
#define ADDRESS_SAMPLE_DATA 5706
#define ADDRESS_REF_TABLE   5710
#define PATTERN_DATA        5714

void Depack_PM20 ( void )
{
  Uchar c1=0x00,c2=0x00;
  short Ref_Max=0;
  long Pats_Address[128];
  long Read_Pats_Address[128];
  Uchar NOP=0x00; /* number of pattern */
  Uchar *ReferenceTable;
  Uchar *Pattern;
  long i=0,j=0,k=0,m=0;
  long Total_Sample_Size=0;
  long PatDataSize=0l;
  long SDAV=0l;
  Uchar FLAG=OFF;
  Uchar poss[37][2];
  Uchar Note,Smp;
  Uchar *Whatever;
  Uchar *WholePatternData;
  long Where = PW_Start_Address;
  FILE *out;/*,*info;*/

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b");*/

  BZERO ( Pats_Address , 128*4 );

  Whatever = (Uchar *) malloc (128);
  BZERO (Whatever, 128);
  /* title */
  fwrite ( &Whatever[0] , 20 , 1 , out );

  /* bypass replaycode routine */
  Where += SAMPLE_DESC;  /* SEEK_SET */

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( &Whatever[32] , 22 , 1 , out );

    in_data[Where+2] /= 2;
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )in_data[Where+7] = 0x01;
    fwrite ( &in_data[Where], 8, 1, out );
    
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    Where += 8;
  }

  /*printf ( "REAL Number of patterns : %d\n" , NOP );*/

  /* read "used" size of pattern table */
  Where = PW_Start_Address + AFTER_REPLAY_CODE + 2;
  NOP = ((in_data[Where]*256)+in_data[Where+1])/2;
  Where += 2;
  /*fprintf ( info, "Number of pattern in pattern list : %d\n" , NOP );*/

  /* write size of pattern list */
  fwrite ( &NOP , 1 , 1 , out );

  /* NoiseTracker restart byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* read pattern addys */
  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
    /*fprintf ( info, "[%3ld] : %ld\n", i, Pats_Address[i] );*/
  }


  /* a little pre-calc code ... no other way to deal with these unknown pattern data sizes ! :( */
  /* so, first, we get the pattern data size .. */
  Where = PW_Start_Address + ADDRESS_REF_TABLE;
  j = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  PatDataSize = (AFTER_REPLAY_CODE + j) - PATTERN_DATA;
  /*fprintf ( info, "Pattern data size : %ld\n" , PatDataSize );*/

  /* go back to pattern data starting address */
  Where = PW_Start_Address + PATTERN_DATA;

  /* now, reading all pattern data to get the max value of note */
  WholePatternData = (Uchar *) malloc (PatDataSize+1);
  BZERO (WholePatternData, PatDataSize+1);
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    WholePatternData[j] = in_data[Where+j];
    WholePatternData[j+1] = in_data[Where+j+1];
    if ( ((WholePatternData[j]*256)+WholePatternData[j+1]) > Ref_Max )
      Ref_Max = ((WholePatternData[j]*256)+WholePatternData[j+1]);
  }

  /* read "reference Table" */
  Where = PW_Start_Address + ADDRESS_REF_TABLE;
  j = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + AFTER_REPLAY_CODE + j;

  Ref_Max += 1;  /* coz 1st value is 0 ! */
  i = Ref_Max * 4; /* coz each block is 4 bytes long */
  ReferenceTable = (Uchar *) malloc ( i );
  BZERO ( ReferenceTable, i );
  for ( j=0 ; j<i ; j++) ReferenceTable[j] = in_data[Where+j];

  /* go back to pattern data starting address */
  Where = PW_Start_Address + PATTERN_DATA;


  c1=0; /* will count patterns */
  k=0; /* current note number */
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    if ( (i%1024) == 0 )
    {
      Read_Pats_Address[c1] = j;
      c1 += 0x01;
      /*      fprintf ( info, " -> new pattern %2d (addy :%ld)\n", c1, j );*/
    }

    m = ((WholePatternData[j]*256)+WholePatternData[j+1])*4;

    Smp  = ReferenceTable[m];
    Smp  = Smp >> 2;
    Note = ReferenceTable[m+1];
      
    Pattern[i]   = (Smp&0xf0);
    Pattern[i]   |= poss[(Note/2)][0];
    Pattern[i+1] = poss[(Note/2)][1];
    Pattern[i+2] = ReferenceTable[m+2];
    Pattern[i+2] |= ((Smp<<4)&0xf0);
    Pattern[i+3] = ReferenceTable[m+3];
    /*fprintf ( info, "[%4ld][%ld][%ld] %2x %2x %2x %2x",i,k%4,j,Pattern[i],Pattern[i+1],Pattern[i+2],Pattern[i+3] );*/

    if ( ( (Pattern[i+2] & 0x0f) == 0x0d ) ||
	 ( (Pattern[i+2] & 0x0f) == 0x0b ) )
    {
      /*fprintf ( info, " <- D or B detected" );*/
      FLAG = ON;
    }
    if ( (FLAG == ON) && ((k%4) == 3) )
    {
      /*fprintf ( info, "\n -> bypassing end of pattern" );*/
      FLAG=OFF;
      while ( (i%1024) != 0)
	i ++;
      i -= 4;
    }

    k += 1;
    i += 4;
    /*fprintf ( info, "\n" );*/
  }

  free ( ReferenceTable );
  free ( WholePatternData );

  /* write pattern table */
  BZERO ( Whatever, 128 );
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<NOP ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
	Whatever[c2] = (Uchar) i;
  while ( i<128 )
    Whatever[i++] = 0x00;
  fwrite ( &Whatever[0], 128, 1, out );

  /* write tag */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';

  fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  fwrite ( &Whatever[2] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );

  free ( Whatever );

  /* write pattern data */
  /* c1 is still the number of patterns stored */
  fwrite ( Pattern, c1*1024, 1, out );

  /* get address of sample data .. and go there */
  Where = PW_Start_Address + ADDRESS_SAMPLE_DATA;
  SDAV = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + AFTER_REPLAY_CODE + SDAV;


  /* read and save sample data */
  /*fprintf ( info, "Total sample size : %ld\n" , Total_Sample_Size );*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "   Promizer 2.0   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*fclose (info );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
