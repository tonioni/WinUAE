/*
 * newtronold.c ... 21 jan 2007
*/
/* testNewtronOld() */
/* Rip_NewtronOld() */
/* Depack_NewtronOld() */


#include "globals.h"
#include "extern.h"


short testNewtronOld ( void )
{
  /* test #1 */
  if ( (PW_i < 11) || ((PW_i+6+1+1024+2)>PW_in_size))
  {
    return BAD;
  }
  
  /* test #1.5 */
  PW_Start_Address = PW_i-11;
  if ( in_data[PW_Start_Address+2] != 0x00 )
  {
    return BAD;
  }

  /* test #2 */
  PW_l=(in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1]+8;
  PW_l = (PW_l/8)-1;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j+=1 )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+8+8*PW_j]*256)+in_data[PW_Start_Address+9+8*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+12+8*PW_j]*256)+in_data[PW_Start_Address+13+8*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+14+8*PW_j]*256)+in_data[PW_Start_Address+15+8*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+11+8*PW_j], in_data[PW_Start_Address+10+8*PW_j] ) == BAD )
    {
            /*printf ( "#2 (start:%ld),(siz:%ld)(loopstart:%ld)(lsiz:%ld)(vol:%d)(fine:%d)(where:%ld)(PW_j:%ld)\n"
	       ,PW_Start_Address,PW_k,PW_m,PW_n, in_data[PW_Start_Address+11+8*PW_j], in_data[PW_Start_Address+10+8*PW_j]
	       ,PW_j*8+8+PW_Start_Address,PW_j );*/
      return BAD; 
    }
    if ( (PW_k==0) && (PW_m==0) && (PW_n==0))
      return BAD;
  }

  if ( PW_WholeSampleSize <= 2 )
  {
    /*printf(  "#3\n" );*/
    return BAD;
  }

  /* test #4 */
  PW_l = in_data[PW_Start_Address+3];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#4 (start:%ld)(indata[0]:%x)\n",PW_Start_Address,in_data[PW_Start_Address] );*/
    return BAD;
  }

  /* test #5 */
  /* PW_l is the size of the patternlist */
  PW_k = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1]+8;
  /* PW_k is the patternlist addy */
  if (((PW_k/8)*8) != PW_k)
  {
    /*printf(  "#5.0\n" );*/
    return BAD;
  }
  
  PW_m = 0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_k+PW_j] > PW_m )
      PW_m = in_data[PW_Start_Address+PW_k+PW_j];
    if ( in_data[PW_Start_Address+PW_k+PW_j] > 0x7f )
    {
      /*printf(  "#5.1 (PW_l:%lx)(PW_k:%lx)(Where:%ld)(val:%x)\n",PW_l,PW_k,PW_Start_Address+PW_k+PW_j,in_data[PW_Start_Address+PW_k+PW_j]);*/
      return BAD;
    }
  }
  PW_m += 1;

  /* #6 */
  /* PW_l is the size of the patternlist */
  /* PW_m contains the number of pattern saved */
  if ( ((PW_m*1024) + PW_k + PW_l + PW_Start_Address) > PW_in_size)
  {
    return BAD;
  }

  /* test #7  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k is the patternlist addy */
  /* PW_l is the size of the patternlist */
  /* PW_m contains the number of pattern saved */
  /* PW_WholeSampleSize is the whole sample size */
  PW_k += PW_l;
  /* PW_k is now the pat data addy */
  for ( PW_j=0 ; PW_j<(256*PW_m) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+PW_k+PW_j*4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      /*printf(  "#7,0\n" );*/
      return BAD;
    }
    PW_n  = in_data[PW_Start_Address+PW_k+PW_j*4]&0x0f;
    PW_n *= 256;
    PW_n += in_data[PW_Start_Address+PW_k+1+PW_j*4];
    if ( (PW_n > 0) && (PW_n<0x71) )
    {
      /*printf ( "#7,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_k+PW_j*4, PW_WholeSampleSize );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_NewtronOld ( void )
{
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_m contains the number of pattern saved */
  /* PW_k is the pat data addy */

  OutputSize = PW_k + PW_WholeSampleSize + (PW_m*1024);

  CONVERT = GOOD;
  Save_Rip ( "Newtron module Old", NewtronOld );
  
  if ( Save_Status == GOOD )
    PW_i += 11;
}

/*
 *   newtronold.c   2007 (c) Asle / ReDoX
 *
 * Converts Newtron Old packed MODs back to PTK MODs
 *
*/

void Depack_NewtronOld ( void )
{
  Uchar *Whatever;
  long i=0,j=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  long patlistaddy=0;
  Uchar patsize = 0;
  Uchar max=0x00;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (Uchar *) malloc (130);
  BZERO ( Whatever , 130 );

  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  /* size of header */
  patlistaddy = (in_data[Where]*256)+in_data[Where+1]+8;
  /* nbr of samples */
  j = (patlistaddy/8)-1;
  
  Where += 8;
  
  for ( i=0 ; i<j ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  Whatever[29] = 0x01;
  while (i++<31)
    fwrite (&Whatever[0],30,1,out);
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* pattern table lenght & Ntk byte */
  patsize = in_data[PW_Start_Address+3];
  fwrite ( &patsize , 1 , 1 , out );
  Whatever[0] = 0x7f;
  fwrite ( &Whatever[0] , 1 , 1 , out );

  Where = patlistaddy+PW_Start_Address;
  BZERO ( Whatever , 130 );
  for ( i=0 ; i<patsize ; i++ )
  {
    if ( in_data[Where+i] > max )
      max = in_data[Where+i];
    Whatever[i] = in_data[Where+i];
  }
  fwrite ( &Whatever[0] , 128 , 1 , out );
  Where += patsize;
  /*printf ( "Number of pattern : %d\n" , Max+1 );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  i = (max+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "    Newtron old   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
