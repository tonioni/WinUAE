/* 22 mar 2003
 * GnuPlayer.c
 * based on XtC's description ! ... good job ! :).
 * 
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testGnuPlayer ( void )
{
  /* test #1 */
  if ( PW_i < 0x92 )
  {
    /*printf ( "#0 (start:%ld) \n", PW_i - 0x92 );*/
    return BAD;
  }

  /* test #2 smp size and loop start */
  PW_WholeSampleSize = 0;
  PW_o = 0;  /* will hold the number of non-null samples */
  PW_Start_Address = PW_i - 0x92;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+20+PW_k*4]*256) + in_data[PW_Start_Address+21+PW_k*4];
    PW_l = (in_data[PW_Start_Address+22+PW_k*4]*256) + in_data[PW_Start_Address+23+PW_k*4];
    if ( PW_l > (PW_j+1) )
    {
      /*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_j != 0 ) PW_o += 1;
    PW_WholeSampleSize += PW_j;
  }

  return GOOD;
}

