#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

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


