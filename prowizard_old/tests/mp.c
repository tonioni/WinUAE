/*  (May 2002)
 *    added test_smps()
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testMP_noID ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_i+375)>PW_in_size))
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-3;
  PW_l=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+8*PW_j]*256)+in_data[PW_Start_Address+1+8*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+4+8*PW_j]*256)+in_data[PW_Start_Address+5+8*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+6+8*PW_j]*256)+in_data[PW_Start_Address+7+8*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+3+8*PW_j], in_data[PW_Start_Address+2+8*PW_j] ) == BAD )
    {
      /*      printf ( "#2 Start:%ld (siz:%ld)(lstart:%ld)(lsiz:%ld)\n", PW_Start_Address,PW_k,PW_m,PW_n );*/
      return BAD; 
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
    /*printf(  "#2,5 (start:%ld)\n",PW_Start_Address );*/
    return BAD;
  }

  /* test #3 */
  PW_l = in_data[PW_Start_Address+248];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#3 (Start:%ld)\n",PW_Start_Address );*/
    return BAD;
  }

  /* test #4 */
  /* PW_l contains the size of the pattern list */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+250+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+250+PW_j];
    if ( in_data[PW_Start_Address+250+PW_j] > 0x7f )
    {
      /*printf(  "#4 (Start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
    if ( PW_j > PW_l+3 )
      if (in_data[PW_Start_Address+250+PW_j] != 0x00)
      {
	/*printf(  "#4,1 (Start:%ld)\n",PW_Start_Address );*/
        return BAD;
      }
  }
  PW_k += 1;

  /* test #5  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  /* PW_WholeSampleSize is the whole sample size */
  PW_m = 0;
  if ( PW_Start_Address + 379 + ((PW_k*256)*4) > PW_in_size )
  {
    return BAD;
  }
  for ( PW_j=0 ; PW_j<((256*PW_k)-4) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+378+PW_j*4+4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      /*printf(  "#5 (Start:%ld)(byte0:%x)(Where:%ld)\n",PW_Start_Address,in_data[PW_Start_Address+378+PW_j*4],PW_Start_Address+378+PW_j*4 );*/
      return BAD;
    }
    PW_l  = in_data[PW_Start_Address+378+PW_j*4]&0x0f;
    PW_l *= 256;
    PW_l += in_data[PW_Start_Address+379+PW_j*4];
    PW_n = in_data[PW_Start_Address+380+PW_j*4]>>4;
    if ( PW_l != 0 )
      PW_m = 1;
    if ( PW_n != 0 )
      PW_o = 1;
    if ( (PW_l > 0) && (PW_l<0x71) )
    {
      /*printf ( "#5,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+378+PW_j*4, PW_l );*/
      return BAD;
    }
  }
  if ( (PW_m == 0) || (PW_o == 0) )
  {
    /* no note ... odd */
    /*printf ("#5,2 (Start:%ld)\n",PW_Start_Address);*/
    return BAD;
  }

  /* test #6  (loopStart+LoopSize > Sample ? ) */
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);
    PW_l = (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2)
          +(((in_data[PW_Start_Address+6+PW_j*8]*256)+in_data[PW_Start_Address+7+PW_j*8])*2);
    if ( PW_l > (PW_k+2) )
    {
      /*printf(  "#6 (Start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}


short testMP_withID ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+6+8*PW_j] > 0x0f )
    {
      return BAD;
    }
  }

  /* test #2 */
  PW_l = in_data[PW_Start_Address+252];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    return BAD;
  }

  /* test #4 */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+254+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+254+PW_j];
    if ( in_data[PW_Start_Address+254+PW_j] > 0x7f )
    {
      return BAD;
    }
  }
  PW_k += 1;

  /* test #5  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  for ( PW_j=0 ; PW_j<(256*PW_k) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+382+PW_j*4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      return BAD;
    }
  }

  return GOOD;
}


