/* testPP10() */
/* Rip_PP10() */
/* Depack_PP10() */


#include "globals.h"
#include "extern.h"


short testPP10 ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_i+246)>=PW_in_size))
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  PW_Start_Address = PW_i-3;

  /* noisetracker byte */
  if ( in_data[PW_Start_Address+249] > 0x7f )
  {
/*printf ( "#1,1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #2 */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);
    PW_l = (((in_data[PW_Start_Address+PW_j*8+4]*256)+in_data[PW_Start_Address+5+PW_j*8])*2);
    /* loop size */
    PW_m = (((in_data[PW_Start_Address+PW_j*8+6]*256)+in_data[PW_Start_Address+7+PW_j*8])*2);
    if ( (PW_m == 0) || (PW_m == PW_l) )
    {
/*printf ( "#1,98 (start:%ld) (PW_k:%ld) (PW_l:%ld) (PW_m:%ld)\n" , PW_Start_Address,PW_k,PW_l,PW_m );*/
      return BAD;
    }
    if ( (PW_l != 0) && (PW_m <= 2) )
    {
/*printf ( "#1,99 (start:%ld) (PW_k:%ld) (PW_l:%ld) (PW_m:%ld)\n" , PW_Start_Address,PW_k,PW_l,PW_m );*/
      return BAD;
    }
    if ( (PW_l+PW_m) > (PW_k+2) )
    {
/*printf ( "#2,0 (start:%ld) (PW_k:%ld) (PW_l:%ld) (PW_m:%ld)\n" , PW_Start_Address,PW_k,PW_l,PW_m );*/
      return BAD;
    }
    if ( (PW_l!=0) && (PW_m == 0) )
    {
/*printf ( "#2,01 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_k;
    /* finetune > 0x0f ? */
    if ( in_data[PW_Start_Address+2+8*PW_j] > 0x0f )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* volume > 0x40 ? */
    if ( in_data[PW_Start_Address+3+8*PW_j] > 0x40 )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2) > PW_k )
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* size > 0xffff ? */
    if ( PW_k > 0xFFFF )
    {
/*printf ( "#2,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#2,4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_WholeSampleSize = whole sample size */

  /* test #3   about size of pattern list */
  PW_l = in_data[PW_Start_Address+248];
  if ( (PW_l > 127) || (PW_l==0) )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get the highest track value */
  PW_k=0;
  for ( PW_j=0 ; PW_j<512 ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+250+PW_j];
    if ( PW_l>PW_k )
      PW_k = PW_l;
  }
  /* PW_k is the highest track number */
  PW_k += 1;
  PW_k *= 64;

  if ( PW_Start_Address + 762 + (PW_k*4) > PW_in_size )
  {
    return BAD;
  }

  /* track data test */
  PW_l=0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+762+PW_j*4] > 0x13 )
    {
/*printf ( "#3,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+762+PW_j*4]&0x0f) == 0x00 ) && (in_data[PW_Start_Address+763+PW_j*4] < 0x71) && (in_data[PW_Start_Address+763+PW_j*4] != 0x00))
    {
      /*      printf ( "#3,2 (start:%ld)(where:%ld)\n",PW_Start_Address,762+PW_j*4 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+762+PW_j*4]&0x0f) != 0x00 ) || (in_data[PW_Start_Address+763+PW_j*4] != 0x00 ))
      PW_l = 1;
  }
  if ( PW_l == 0 )
  {
    /* only some empty patterns */
    return BAD;
  }
  PW_k *= 4;

  /* PW_WholeSampleSize is the sample data size */
  /* PW_k is the track data size */
  return GOOD;
}



void Rip_PP10 ( void )
{
  /* PW_k is still the size of the track data */
  /* PW_WholeSampleSize is still the sample data size */

  OutputSize = PW_WholeSampleSize + PW_k + 762;

  CONVERT = GOOD;
  Save_Rip ( "ProPacker v1.0 Exe-file", PP10 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}



/*
 *   ProPacker_v1.0   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk ProPacker v1 MODs
 *
 * Update: 28/11/99
 *     - removed fopen() and all attached functions.
 *     - overall speed and size optimizings.
 * Update: 19/04/00 (all pointed out by Thomas Neumann)
 *     - replen bug correction
*/

void Depack_PP10 ( void )
{
  Uchar Tracks_Numbers[4][128];
  Uchar Pat_Pos;
  Uchar *Whatever;
  short Max;
  long i=0,j=0,k=0;
  long WholeSampleSize=0;
  long Where=PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Tracks_Numbers , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  if (!out)
    return;

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 6 , 1 , out );

    Whatever[32] = in_data[Where+6];
    Whatever[33] = in_data[Where+7];
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )
      Whatever[33] = 0x01;
    fwrite ( &Whatever[32] , 2 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* read and write pattern table lenght */
  Pat_Pos = in_data[Where++];
  fwrite ( &Pat_Pos , 1 , 1 , out );
  /*printf ( "Size of pattern list : %d\n" , Pat_Pos );*/

  /* read and write NoiseTracker byte */
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* read track list and get highest track number */
  Max = 0;
  for ( j=0 ; j<4 ; j++ )
  {
    for ( i=0 ; i<128 ; i++ )
    {
      Tracks_Numbers[j][i] = in_data[Where++];
      if ( Tracks_Numbers[j][i] > Max )
	Max = Tracks_Numbers[j][i];
    }
  }
  /*printf ( "highest track number : %d\n" , Max+1 );*/

  /* write pattern table "as is" ... */
  for (Whatever[0]=0 ; Whatever[0]<Pat_Pos ; Whatever[0]+=0x01 )
    fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[256] , (128-Pat_Pos) , 1 , out );
  /* Where is reassigned later */

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* track/pattern data */

  for ( i=0 ; i<Pat_Pos ; i++ )
  {
/*fprintf ( info , "\n\n\nPattern %ld :\n" , i );*/
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      Where = PW_Start_Address + 762+(Tracks_Numbers[j][i]*256);
/*fprintf ( info , "Voice %ld :\n" , j );*/
      for ( k=0 ; k<64 ; k++ )
      {
        Whatever[k*16+j*4]   = in_data[Where++];
        Whatever[k*16+j*4+1] = in_data[Where++];
        Whatever[k*16+j*4+2] = in_data[Where++];
        Whatever[k*16+j*4+3] = in_data[Where++];
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/


  /* now, lets put file pointer at the beginning of the sample datas */
  Where = PW_Start_Address + 762 + ((Max+1)*256);

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "  ProPacker v1.0  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
