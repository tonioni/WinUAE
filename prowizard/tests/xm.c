#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testXM ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 336) > PW_in_size)
  {
    /*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  if ( (in_data[PW_Start_Address + 37] != 0x1A ) || (in_data[PW_Start_Address+58] != 0x04) )
  {
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get header siz */
  PW_l = ( (in_data[PW_Start_Address+63]*256*256*256)+
	   (in_data[PW_Start_Address+62]*256*256)+
	   (in_data[PW_Start_Address+61]*256)+
	   in_data[PW_Start_Address+60] );
  /* get number of patterns */
  PW_j = (in_data[PW_Start_Address + 71]*256) + in_data[PW_Start_Address + 70];
  /* get number of instruments */
  PW_k = (in_data[PW_Start_Address + 73]*256) + in_data[PW_Start_Address + 72];

  if ( (PW_j>256)||(PW_k>128) )
  {
printf ( "#3 Start:%ld\n" , PW_Start_Address );
    return BAD;
  }

  PW_l += 60; /* points now on the first pattern data */
  

  /* PW_l is the header size and points on the first pattern */
  /* PW_j is the number of pattern */
  /* PW_k is the number of instruments */
  return GOOD;
}

