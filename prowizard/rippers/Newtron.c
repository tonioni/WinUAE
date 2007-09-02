/*
 * newtron.c ... 9 mar 2003/21 jan 2007
*/
/* testNewtron() */
/* Rip_Newtron() */
/* Depack_Newtron() */


#include "globals.h"
#include "extern.h"


short testNewtron ( void )
{
  /* test #1 */
  if ( (PW_i < 7) || ((PW_i+373+1024+2)>PW_in_size))
  {
    return BAD;
  }
  
  /* test #1.5 */
  if ( in_data[PW_i-6] != 0x00 )
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-7;
  PW_l=0;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j+=1 )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+4+8*PW_j]*256)+in_data[PW_Start_Address+5+8*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+8+8*PW_j]*256)+in_data[PW_Start_Address+9+8*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+10+8*PW_j]*256)+in_data[PW_Start_Address+11+8*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+7+8*PW_j], in_data[PW_Start_Address+6+8*PW_j] ) == BAD )
    {
      /*      printf ( "#2 (start:%ld),(siz:%ld)(loopstart:%ld)(lsiz:%ld)(vol:%d)(fine:%d)(where:%ld)(PW_j:%ld)\n"
	       ,PW_Start_Address,PW_k,PW_m,PW_n, in_data[PW_Start_Address+7+8*PW_j], in_data[PW_Start_Address+6+8*PW_j]
	       ,PW_j*8+4+PW_Start_Address,PW_j );*/
      return BAD; 
    }
  }

  if ( PW_WholeSampleSize <= 2 )
  {
    /*    printf(  "#3\n" );*/
    return BAD;
  }

  /* test #4 */
  PW_l = in_data[PW_Start_Address];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*    printf(  "#4 (start:%ld)(indata[0]:%x)\n",PW_Start_Address,in_data[PW_Start_Address] );*/
    return BAD;
  }

  /* test #5 */
  /* PW_l contains the size of the pattern list */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+252+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+252+PW_j];
    if ( in_data[PW_Start_Address+252+PW_j] > 0x7f )
    {
      /*printf(  "#5\n" );*/
      return BAD;
    }
  }
  PW_k += 1;

  /* #6 */
  if ( ((PW_k*1024) + 380) != ((in_data[PW_Start_Address+2]*256)+in_data[PW_Start_Address+3]+4))
  {
    return BAD;
  }

  /* test #7  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  /* PW_WholeSampleSize is the whole sample size */
  for ( PW_j=0 ; PW_j<(256*PW_k) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+380+PW_j*4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      /*printf(  "#7,0\n" );*/
      return BAD;
    }
    PW_m  = in_data[PW_Start_Address+380+PW_j*4]&0x0f;
    PW_m *= 256;
    PW_m += in_data[PW_Start_Address+381+PW_j*4];
    if ( (PW_m > 0) && (PW_m<0x71) )
    {
      /*printf ( "#7,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+380+PW_j*4, PW_WholeSampleSize );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_Newtron ( void )
{
  /* PW_WholeSampleSize is the whole sample size :) */

  PW_k = (in_data[PW_Start_Address+2]*256) + in_data[PW_Start_Address+3];
  
  OutputSize = PW_k + PW_WholeSampleSize + 4;

  CONVERT = GOOD;
  Save_Rip ( "Newtron module", Newtron );
  
  if ( Save_Status == GOOD )
    PW_i += 7;
}

/*
 *   newtron.c   2003-2007 (c) Asle / ReDoX
 *
 * Converts Newtron packed MODs back to PTK MODs
 *
 * Last update: 09 mar 2003
 * fixed again : 21 jan 2007
*/

void Depack_Newtron ( void )
{
  Uchar *Whatever;
  long i=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (Uchar *) malloc (64);
  BZERO ( Whatever , 64 );

  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  Where += 4;

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* pattern table lenght & Ntk byte */
  fwrite ( &in_data[PW_Start_Address] , 1 , 1 , out );
  Whatever[0] = 0x7f;
  fwrite ( &Whatever[0] , 1 , 1 , out );

  Whatever[32] = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i] > Whatever[32] )
      Whatever[32] = in_data[Where+i];
  }
  fwrite ( &in_data[Where] , 128 , 1 , out );
  Where += 128;
  /*printf ( "Number of pattern : %d\n" , Max+1 );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  i = (Whatever[32]+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "      Newtron     " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
