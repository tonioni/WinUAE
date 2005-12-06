/*
 * Even in these poor lines, I managed to insert a bug :)
 * 
 * update: 19/04/00
 *  - bug correction (really testing volume !)
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSKYT ( void )
{
  /* test 1 */
  if ( PW_i < 256 )
  {
    return BAD;
  }

  /* test 2 */
  PW_WholeSampleSize = 0;
  PW_Start_Address = PW_i-256;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+3+8*PW_j] > 0x40 )
    {
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+8*PW_j]*256)+in_data[PW_Start_Address+1+8*PW_j])*2);
  }

  return GOOD;
}

