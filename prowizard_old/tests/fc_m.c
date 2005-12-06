#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testFC_M ( void )
{
  /* test 1 */
  PW_Start_Address = PW_i;
  if ( in_data[PW_Start_Address + 4] != 0x01 )
  {
    return BAD;
  }

  /* test 2 */
  if ( in_data[PW_Start_Address + 5] != 0x00 )
  {
    return BAD;
  }

  /* test 3 */
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+37+8*PW_j] > 0x40 )
    {
      return BAD;
    }
  }

  return GOOD;
}

