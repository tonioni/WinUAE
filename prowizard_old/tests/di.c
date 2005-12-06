/* (3rd of April 2000)
 *   bugs pointed out by Thomas Neumann .. thx :)
 * (May 2002)
 *   added test_smps()
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testDI ( void )
{
  /* test #1 */
  if ( PW_i < 17 )
  {
    return BAD;
  }

  /* test #2  (number of sample) */
  PW_Start_Address = PW_i-17;
  PW_k = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1];
  if ( PW_k > 31 )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3 (finetunes and whole sample size) */
  /* PW_k = number of samples */
  PW_WholeSampleSize = 0;
  PW_l = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_o = (((in_data[PW_Start_Address+(PW_j*8)+14]*256)+in_data[PW_Start_Address+(PW_j*8)+15])*2);
    PW_m = (((in_data[PW_Start_Address+(PW_j*8)+18]*256)+in_data[PW_Start_Address+(PW_j*8)+19])*2);
    PW_n = (((in_data[PW_Start_Address+(PW_j*8)+20]*256)+in_data[PW_Start_Address+(PW_j*8)+21])*2);
    if ( (PW_o > 0xffff) ||
         (PW_m > 0xffff) ||
         (PW_n > 0xffff) )
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( test_smps(PW_o, PW_m, PW_n, in_data[PW_Start_Address+17+PW_j*8], in_data[PW_Start_Address+16+PW_j*8] ) == BAD )
      return BAD;
    /* gets total size of samples */
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#2,4\n" );*/
    return BAD;
  }

  /* test #4 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the whole sample size */
  /* PW_k is still the number of sample */
  PW_m = PW_k;
  PW_j = (in_data[PW_Start_Address+2]*256*256*256)
         +(in_data[PW_Start_Address+3]*256*256)
         +(in_data[PW_Start_Address+4]*256)
         +in_data[PW_Start_Address+5];
  /* PW_j is the address of pattern table now */
  PW_k = (in_data[PW_Start_Address+6]*256*256*256)
         +(in_data[PW_Start_Address+7]*256*256)
         +(in_data[PW_Start_Address+8]*256)
         +in_data[PW_Start_Address+9];
  /* PW_k is the address of the pattern data */
  PW_l = (in_data[PW_Start_Address+10]*256*256*256)
         +(in_data[PW_Start_Address+11]*256*256)
         +(in_data[PW_Start_Address+12]*256)
         +in_data[PW_Start_Address+13];
  /* PW_l is the address of the sample data */
  if ( (PW_k <= PW_j)||(PW_l<=PW_j)||(PW_l<=PW_k) )
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k-PW_j) > 128 )
  {
/*printf ( "#3,1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k > PW_in_size)||(PW_l>PW_in_size)||(PW_j>PW_in_size) )
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #4,1 :) */
  PW_m *= 8;
  PW_m += 2;
  if ( PW_j < PW_m )
  {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #5 */
  if ( (PW_k + PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern table reliability */
  for ( PW_m=PW_j ; PW_m<(PW_k-1) ; PW_m++ )
  {
    if ( in_data[PW_Start_Address + PW_m] > 0x80 )
    {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* test #6  ($FF at the end of pattern list ?) */
  if ( in_data[PW_Start_Address+PW_k-1] != 0xFF )
  {
/*printf ( "#6,1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #7 (addres of sample data > $FFFF ? ) */
  /* PW_l is still the address of the sample data */
  if ( PW_l > 65535 )
  {
/*printf ( "#7 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}

