#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testKSM ( void )
{
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 1536) > PW_in_size)
    return BAD;

  /* test "a" */
  if ( in_data[PW_Start_Address+15] != 'a' )
    return BAD;

  /* test volumes */
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
    if ( in_data[PW_Start_Address+54+PW_k*32] > 0x40 )
      return BAD;

  /* test tracks data */
  /* first, get the highest track number .. */
  PW_j = 0;
  for ( PW_k=0 ; PW_k<1024 ; PW_k ++ )
  {
    if ( in_data[PW_Start_Address+PW_k+512] == 0xFF )
      break;
    if ( in_data[PW_Start_Address+PW_k+512] > PW_j )
      PW_j = in_data[PW_Start_Address+PW_k+512];
  }
  if ( PW_k == 1024 )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_j == 0 )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* so, now, PW_j is the highest track number (first is 00h !!) */
  /* real test on tracks data starts now */
  /* first, test if we don't get out of the file */
  if ( (PW_Start_Address + 1536 + PW_j*192 + 64*3) > PW_in_size )
    return BAD;
  /* now testing tracks */
  for ( PW_k = 0 ; PW_k <= PW_j ; PW_k++ )
    for ( PW_l=0 ; PW_l < 64 ; PW_l++ )
      if ( in_data[PW_Start_Address+1536+PW_k*192+PW_l*3] > 0x24 )
	return BAD;

  /* PW_j is still the highest track number */
  return GOOD;
}

