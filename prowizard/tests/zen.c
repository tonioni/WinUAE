#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testZEN ( void )
{
  /* test #1 */
  if ( PW_i<9 )
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-9;
  PW_l = ( (in_data[PW_Start_Address]*256*256*256)+
	   (in_data[PW_Start_Address+1]*256*256)+
	   (in_data[PW_Start_Address+2]*256)+
	   in_data[PW_Start_Address+3] );
  if ( (PW_l<502) || (PW_l>2163190l) || (PW_l>(PW_in_size-PW_Start_Address)) )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l is the address of the pattern list */

  /* volumes */
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    if ( (( PW_Start_Address + 9 + (16*PW_k) ) > PW_in_size )||
	 (( in_data[PW_Start_Address + 9 + (16*PW_k)] > 0x40 )))
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* fine */
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    PW_j = (in_data[PW_Start_Address+6+(PW_k*16)]*256)+in_data[PW_Start_Address+7+(PW_k*16)];
    if ( ((PW_j/72)*72) != PW_j )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* smp sizes .. */
  PW_n = 0;
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    PW_o = (in_data[PW_Start_Address+10+PW_k*16]*256)+in_data[PW_Start_Address+11+PW_k*16];
    PW_m = (in_data[PW_Start_Address+12+PW_k*16]*256)+in_data[PW_Start_Address+13+PW_k*16];
    PW_j = ((in_data[PW_Start_Address+14+PW_k*16]*256*256*256)+
	    (in_data[PW_Start_Address+15+PW_k*16]*256*256)+
	    (in_data[PW_Start_Address+16+PW_k*16]*256)+
	    in_data[PW_Start_Address+17+PW_k*16] );
    PW_o *= 2;
    PW_m *= 2;
    /* sample size and loop size > 64k ? */
    if ( (PW_o > 0xFFFF)||
	 (PW_m > 0xFFFF) )
    {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* sample address < pattern table address ? */
    if ( PW_j < PW_l )
    {
/*printf ( "#4,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* too big an address ? */
    if ( PW_j > PW_in_size )
    {
/*printf ( "#4,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* get the nbr of the highest sample address and its size */
    if ( PW_j > PW_n )
    {
      PW_n = PW_j;
      PW_WholeSampleSize = PW_o;
    }
  }
  /* PW_n is the highest sample data address */
  /* PW_WholeSampleSize is the size of the same sample */

  /* test size of the pattern list */
  PW_j = in_data[PW_Start_Address+5];
  if ( (PW_j > 0x7f) || (PW_j == 0) )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }


  /* test if the end of pattern list is $FFFFFFFF */
  if ( (in_data[PW_Start_Address+PW_l+PW_j*4] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+1] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+2] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+3] != 0xFF ) )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_n is the highest address of a sample data */
  /* PW_WholeSampleSize is its size */
  return GOOD;
}

