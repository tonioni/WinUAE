#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTP3 ( void )
{

  PW_Start_Address = PW_i;

  /* number of sample */
  PW_l = ( (in_data[PW_Start_Address+28]*256)+
	   in_data[PW_Start_Address+29] );
  if ( (((PW_l/8)*8) != PW_l) || (PW_l == 0) )
  {
/*printf ( "#2 Start: %ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_l /= 8;
  /* PW_l is the number of sample */

  /* test finetunes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+30+PW_k*8] > 0x0f )
    {
/*printf ( "#3 Start: %ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+31+PW_k*8] > 0x40 )
    {
/*printf ( "#4 Start: %ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    /* size */
    PW_j = (in_data[PW_Start_Address+PW_k*8+32]*256)+in_data[PW_Start_Address+PW_k*8+33];
    /* loop start */
    PW_m = (in_data[PW_Start_Address+PW_k*8+34]*256)+in_data[PW_Start_Address+PW_k*8+35];
    /* loop size */
    PW_n = (in_data[PW_Start_Address+PW_k*8+36]*256)+in_data[PW_Start_Address+PW_k*8+37];
    PW_j *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( (PW_j > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m + PW_n) > (PW_j+2) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m != 0) && (PW_n == 0) )
    {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* pattern list size */
  PW_j = in_data[PW_Start_Address+PW_l*8+31];
  if ( (PW_j==0) || (PW_j>128) )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_j is the size of the pattern list */
  /* PW_l is the number of sample */
  /* PW_WholeSampleSize is the sample data size */
  return GOOD;
}

