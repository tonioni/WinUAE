#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPP30 ( void )
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
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+PW_j*8+1])*2);
    PW_WholeSampleSize += PW_k;
    /* finetune > 0x0f ? */
    if ( in_data[PW_Start_Address+8*PW_j+2] > 0x0f )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* volume > 0x40 ? */
    if ( in_data[PW_Start_Address+8*PW_j+3] > 0x40 )
    {
/*printf ( "#2,0 (start:%ld)\n" , PW_Start_Address );*/
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

  /* test #4  track data value *4 ? */
  /* PW_WholeSampleSize is the whole sample size */
  PW_m = 0;
  if ( ((PW_k*2)+PW_Start_Address+763) > PW_in_size )
  {
    return BAD;
  }
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_l = (in_data[PW_Start_Address+762+PW_j*2]*256)+in_data[PW_Start_Address+763+PW_j*2];
    if ( PW_l > PW_m )
      PW_m = PW_l;
    if ( ((PW_l*4)/4) != PW_l  )
    {
/*printf ( "#4 (start:%ld)(where:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_j*2+762 );*/
      return BAD;
    }
  }

  /* test #5  reference table size *4 ? */
  /* PW_m is the highest reference number */
  PW_k *= 2;
  PW_m /= 4;
  PW_l = (in_data[PW_Start_Address+PW_k+762]*256*256*256)
    +(in_data[PW_Start_Address+PW_k+763]*256*256)
    +(in_data[PW_Start_Address+PW_k+764]*256)
    +in_data[PW_Start_Address+PW_k+765];
  if ( PW_l > 65535 )
  {
    return BAD;
  }
  if ( PW_l != ((PW_m+1)*4) )
  {
/*printf ( "#5 (start:%ld)(where:%ld)\n" , PW_Start_Address,(PW_Start_Address+PW_k+762) );*/
    return BAD;
  }

  /* test #6  data in reference table ... */
  for ( PW_j=0 ; PW_j<(PW_l/4) ; PW_j++ )
  {
    /* volume > 41 ? */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0c) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x41 ) )
    {
/*printf ( "#6 (vol > 40 at : %ld)\n" , PW_Start_Address+PW_k+766+PW_j*4+2 );*/
      return BAD;
    }
    /* break > 40 ? */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0d) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x40 ) )
    {
/*printf ( "#6,1\n" );*/
      return BAD;
    }
    /* jump > 128 */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0b) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x7f ) )
    {
/*printf ( "#6,2\n" );*/
      return BAD;
    }
    /* smp > 1f ? */
    if ((in_data[PW_Start_Address+PW_k+766+PW_j*4]&0xf0)>0x10)
    {
/*printf ( "#6,3\n" );*/
      return BAD;
    }
  }
  /* PW_WholeSampleSize is the whole sample size */

  return GOOD;
}
