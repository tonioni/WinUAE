/* (27 dec 2001)
 *   added some checks to prevent readings outside of input file (in test 1)
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

short testEUREKA ( void )
{
  /* test 1 */
  if ( (PW_i < 45) || ((PW_Start_Address+950)>=PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-45;
  PW_j = in_data[PW_Start_Address+950];
  if ( (PW_j==0) || (PW_j>127) )
  {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3  finetunes & volumes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+42+PW_k*30]*256) + in_data[PW_Start_Address+43+PW_k*30];
    PW_m = (in_data[PW_Start_Address+46+PW_k*30]*256) + in_data[PW_Start_Address+47+PW_k*30];
    PW_n = (in_data[PW_Start_Address+48+PW_k*30]*256) + in_data[PW_Start_Address+49+PW_k*30];
    PW_o *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( (PW_o > 0xffff) ||
         (PW_m > 0xffff) ||
         (PW_n > 0xffff) )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( test_smps ( PW_o+2, PW_m, PW_n, in_data[PW_Start_Address+45+PW_k*30], in_data[PW_Start_Address+44+PW_k*30] ) == BAD)
      return BAD;
    PW_WholeSampleSize += PW_o;
  }


  /* test 4 */
  PW_l = (in_data[PW_Start_Address+1080]*256*256*256)
    +(in_data[PW_Start_Address+1081]*256*256)
    +(in_data[PW_Start_Address+1082]*256)
    +in_data[PW_Start_Address+1083];
  if ( (PW_l+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_l < 1084 )
  {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_m=0;
  /* pattern list */
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    PW_n = in_data[PW_Start_Address+952+PW_k];
    if ( PW_n > PW_m )
      PW_m = PW_n;
    if ( PW_n > 127 )
    {
/*printf ( "#4,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  PW_k += 2; /* to be sure .. */
  while ( PW_k != 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_k] != 0 )
    {
/*printf ( "#4,3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_k += 1;
  }
  PW_m += 1;
  /* PW_m is the highest pattern number */


  /* test #5 */
  /* PW_j is still the size if the pattern table */
  /* PW_l is still the address of the sample data */
  /* PW_m is the highest pattern number */
  PW_n=0;
  PW_j=999999l;
  for ( PW_k=0 ; PW_k<(PW_m*4) ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k*2+1084]*256)+in_data[PW_Start_Address+PW_k*2+1085];
    if ( (PW_o > PW_l) || (PW_o < 1084) )
    {
/*printf ( "#5 (Start:%ld)(PW_k:%ld)(PW_j:%ld)(PW_l:%ld)\n" , PW_Start_Address,PW_k,PW_j,PW_l );*/
      return BAD;
    }
    if ( PW_o > PW_n )
      PW_n = PW_o;
    if ( PW_o < PW_j )
      PW_j = PW_o;
  }
  /* PW_o is the highest track address */
  /* PW_j is the lowest track address */

  /* test track datas */
  /* last track wont be tested ... */
  for ( PW_k=PW_j ; PW_k<PW_o ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0xC0 )
      continue;
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x80 )
    {
      PW_k += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x40 )
    {
      if ( ((in_data[PW_Start_Address+PW_k]&0x3F) == 0x00) &&
	   (in_data[PW_Start_Address+PW_k+1] == 0x00) )
      {
/*printf ( "#6 Start:%ld (at:%x)\n" , PW_Start_Address , PW_k );*/
	return BAD;
      }
      PW_k += 1;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x00 )
    {
      if ( in_data[PW_Start_Address+PW_k] > 0x13 )
      {
/*printf ( "#6,1 Start:%ld (at:%x)\n" , PW_Start_Address , PW_k );*/
	return BAD;
      }
      PW_k += 3;
      continue;
    }
  }

  return GOOD;
}
