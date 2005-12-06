#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


short testFuchsTracker ( void )
{
  /* test #1 */
  if ( PW_i<192 )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-192;

  /* all sample size */
  PW_j = ((in_data[PW_Start_Address+10]*256*256*256)+
          (in_data[PW_Start_Address+11]*256*256)+
          (in_data[PW_Start_Address+12]*256)+
           in_data[PW_Start_Address+13] );
  if ( (PW_j <= 2) || (PW_j >= (65535*16)) )
  {
/*printf ( "#1,1\n" );*/
    return BAD;
  }



  /* samples descriptions */
  PW_m=0;
  for ( PW_k = 0 ; PW_k < 16 ; PW_k ++ )
  {
    /* size */
    PW_o = (in_data[PW_Start_Address+PW_k*2+14]*256)+in_data[PW_Start_Address+PW_k*2+15];
    /* loop start */
    PW_n = (in_data[PW_Start_Address+PW_k*2+78]*256)+in_data[PW_Start_Address+PW_k*2+79];

    /* volumes */
    if ( in_data[PW_Start_Address+46+PW_k*2] > 0x40 )
    {
/*printf ( "#2\n" );*/
      return BAD;
    }
    /* size < loop start ? */
    if ( PW_o < PW_n )
    {
/*printf ( "#2,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_m += PW_o;
  }

  /* PW_m is the size of all samples (in descriptions) */
  /* PW_j is the sample data sizes (header) */
  /* size<2  or  size > header sample size ? */
  if ( (PW_m <= 2) || (PW_m > PW_j) )
  {
/*printf ( "#2,2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get highest pattern number in pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<40 ; PW_j++ )
  {
    PW_n = in_data[PW_Start_Address+PW_j*2+113];
    if ( PW_n > 40 )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_n > PW_k )
      PW_k = PW_n;
  }

  /* PW_m is the size of all samples (in descriptions) */
  /* PW_k is the highest pattern data -1 */
  /* input file not long enough ? */
  PW_k += 1;
  PW_k *= 1024;
  if ( (PW_k+200) > PW_in_size )
  {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_m is the size of all samples (in descriptions) */
  /* PW_k is the pattern data size */

  return GOOD;
}

