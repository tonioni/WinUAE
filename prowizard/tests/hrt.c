#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testHRT ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[45+PW_j*30+PW_Start_Address] > 0x40 )
    {
      return BAD;
    }
  }

  return GOOD;
}


