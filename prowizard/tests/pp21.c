/*
 * 5th, oct 2001 : strenghtened a test that hanged the prog sometimes
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPP21 ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_i+891)>=PW_in_size))
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-3;
  PW_WholeSampleSize=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);
    PW_WholeSampleSize += PW_k;
    /* finetune > 0x0f ? */
    if ( in_data[PW_Start_Address+2+8*PW_j] > 0x0f )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2) > PW_k )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

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
  if ( (PW_k*2) + PW_Start_Address + 763 > PW_in_size )
  {
/* printf ( "#3,5 (start:%ld)\n" , PW_Start_Address)*/
    return BAD;
  }

  /* test #4  track data value > $4000 ? */
  PW_m = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_l = (in_data[PW_Start_Address+762+PW_j*2]*256)+in_data[PW_Start_Address+763+PW_j*2];
    if ( PW_l > PW_m )
      PW_m = PW_l;
    if ( PW_l > 0x4000 )
    {
/*printf ( "#4 (start:%ld)(where:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_j*2+762 );*/
      return BAD;
    }
  }

  /* test #5  reference table size *4 ? */
  PW_k *= 2;
  PW_l = (in_data[PW_Start_Address+PW_k+762]*256*256*256)
    +(in_data[PW_Start_Address+PW_k+763]*256*256)
    +(in_data[PW_Start_Address+PW_k+764]*256)
    +in_data[PW_Start_Address+PW_k+765];
  if ( PW_l != ((PW_m+1)*4) )
  {
/*printf ( "#5 (start:%ld)(where:%ld)\n" , PW_Start_Address,(PW_Start_Address+PW_k+762) );*/
    return BAD;
  }

  return GOOD;
}


