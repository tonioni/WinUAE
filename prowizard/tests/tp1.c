#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif 

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTP1 ( void )
{
  PW_Start_Address = PW_i;

  /* size of the module */
  PW_WholeSampleSize = ( (in_data[PW_Start_Address+4]*256*256*256)+
			 (in_data[PW_Start_Address+5]*256*256)+
			 (in_data[PW_Start_Address+6]*256)+
			 in_data[PW_Start_Address+7] );
  if ( (PW_WholeSampleSize < 794) || (PW_WholeSampleSize > 2129178l) )
  {
    return BAD;
  }

  /* test finetunes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+32+PW_k*8] > 0x0f )
    {
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+33+PW_k*8] > 0x40 )
    {
      return BAD;
    }
  }

  /* sample data address */
  PW_l = ( (in_data[PW_Start_Address+28]*256*256*256)+
	   (in_data[PW_Start_Address+29]*256*256)+
	   (in_data[PW_Start_Address+30]*256)+
	   in_data[PW_Start_Address+31] );
  if ( (PW_l == 0) || (PW_l > PW_WholeSampleSize ) )
  {
    return BAD;
  }

  /* test sample sizes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+PW_k*8+34]*256)+in_data[PW_Start_Address+PW_k*8+35];
    PW_m = (in_data[PW_Start_Address+PW_k*8+36]*256)+in_data[PW_Start_Address+PW_k*8+37];
    PW_n = (in_data[PW_Start_Address+PW_k*8+38]*256)+in_data[PW_Start_Address+PW_k*8+39];
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
    if ( (PW_m != 0) && (PW_n <= 2) )
    {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* pattern list size */
  PW_l = in_data[PW_Start_Address+281];
  if ( (PW_l==0) || (PW_l>128) )
  {
    return BAD;
  }

  /* PW_WholeSampleSize is the size of the module :) */
  return GOOD;
}

