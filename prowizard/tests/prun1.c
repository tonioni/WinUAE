#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPRUN1 ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  if ( in_data[PW_Start_Address+951] != 0x7f )
  {
    return BAD;
  }

  /* test 3 */
  if ( in_data[PW_Start_Address+950] > 0x7f )
  {
    return BAD;
  }
  return GOOD;
}
