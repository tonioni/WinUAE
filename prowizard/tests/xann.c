#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testXANN ( void )
{
  /* test 1 */
  if ( PW_i < 3 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i - 3;
  for ( PW_l=0 ; PW_l<128 ; PW_l++ )
  {
    PW_j = (in_data[PW_Start_Address+PW_l*4]*256*256*256)
          +(in_data[PW_Start_Address+PW_l*4+1]*256*256)
          +(in_data[PW_Start_Address+PW_l*4+2]*256)
          +in_data[PW_Start_Address+PW_l*4+3];
    PW_k = (PW_j/4)*4;
    if ( (PW_k != PW_j) || (PW_j>132156) )
    {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test 3 */
  if ( (PW_in_size - PW_Start_Address) < 2108)
  {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test 4 */
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
    if ( (in_data[PW_Start_Address+3+PW_j*4] != 0x3c) &&
	 (in_data[PW_Start_Address+3+PW_j*4] != 0x00) )
    {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test 5 */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+519+16*PW_j] > 0x40 )
    {
/*printf ( "#5 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+530+PW_j*16]*256)+in_data[PW_Start_Address+531+PW_j*16])*2);
  }

  /* test #6  (address of samples) */
  for ( PW_l=0 ; PW_l<30 ; PW_l++ )
  {
    PW_k = (in_data[PW_Start_Address+526+16*PW_l]*256*256*256)
      +(in_data[PW_Start_Address+527+16*PW_l]*256*256)
      +(in_data[PW_Start_Address+528+16*PW_l]*256)
      +in_data[PW_Start_Address+529+16*PW_l];
    PW_j = (((in_data[PW_Start_Address+524+16*PW_l]*256)
      +in_data[PW_Start_Address+525+16*PW_l])*2);
    PW_m = (in_data[PW_Start_Address+520+16*(PW_l+1)]*256*256*256)
      +(in_data[PW_Start_Address+521+16*(PW_l+1)]*256*256)
      +(in_data[PW_Start_Address+522+16*(PW_l+1)]*256)
      +in_data[PW_Start_Address+523+16*(PW_l+1)];
    if ( (PW_k<2108) || (PW_m<2108) )
    {
/*printf ( "#6 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_k > PW_m )
    {
/*printf ( "#6,1 (Start:%ld) (PW_k:%ld) (PW_j:%ld) (PW_m:%ld)\n" , PW_Start_Address,PW_k,PW_j,PW_m );*/
      return BAD;
    }
  }

  /* test #7  first pattern data .. */
  for ( PW_j=0 ; PW_j<256 ; PW_j++ )
  {
    PW_k = (in_data[PW_Start_Address+PW_j*4+1085]/2);
    PW_l = PW_k*2;
    if ( in_data[PW_Start_Address+PW_j*4+1085] != PW_l )
    {
/*printf ( "#7 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}

