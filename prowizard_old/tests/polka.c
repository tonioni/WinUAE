/*  (Mar 2003)
 *    polka.c
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPolka ( void )
{
  /* test #1 */
  if ( (PW_i < 0x438) || ((PW_i+0x830)>PW_in_size))
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-0x438;
  PW_l=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+42+30*PW_j]*256)+in_data[PW_Start_Address+43+30*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+30*PW_j]*256)+in_data[PW_Start_Address+47+30*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+30*PW_j]*256)+in_data[PW_Start_Address+49+30*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+45+30*PW_j], in_data[PW_Start_Address+44+30*PW_j] ) == BAD )
    {
      /*      printf ("#2 (start:%ld)(siz:%ld)(lstart:%ld)(lsiz:%ld)(vol:%d)(fine:%d)(Where:%ld)\n"
	      ,PW_Start_Address,PW_k,PW_m,PW_n
	      ,in_data[PW_Start_Address+0x2d +30+PW_j],in_data[PW_Start_Address+0x2c +30*PW_j]
	      ,PW_Start_Address+0x2a + 30*PW_j);*/
      return BAD; 
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
    /*    printf(  "#2,1\n" );*/
    return BAD;
  }

  /* test #3 */
  PW_l = in_data[PW_Start_Address+0x3b6];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#3\n" );*/
    return BAD;
  }

  /* test #4 */
  /* PW_l contains the size of the pattern list */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+0x3b8+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+0x3b8+PW_j];
    if ( in_data[PW_Start_Address+0x3b8+PW_j] > 0x7f )
    {
      /*printf(  "#4 (start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
  }
  PW_k += 1;

  /* test #5  notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  /* PW_WholeSampleSize contains the whole sample size :) */
  for ( PW_j=0 ; PW_j<(256*PW_k) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+0x43c+PW_j*4];
    if ( (PW_l > 72) || (((PW_l/2)*2)!=PW_l) )
    {
      /*printf(  "#5 (start:%ld)(note:%ld)(where:%ld)\n",PW_Start_Address,PW_l,PW_Start_Address+0x43c +PW_j*4 );*/
      return BAD;
    }
    PW_l  = in_data[PW_Start_Address+0x43e +PW_j*4]&0xf0;
    PW_m  = in_data[PW_Start_Address+0x43d +PW_j*4];
    if ( (PW_l != 0) || (PW_m > 0x1f))
    {
      /*printf ( "#5,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+0x43c +PW_j*4, PW_m );*/
      return BAD;
    }
  }

  return GOOD;
}
