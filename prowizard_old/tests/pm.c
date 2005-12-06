/* update on the 3rd of april 2000 */
/* some advices by Thomas Neumann .. thx */

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

/* Power Music */
short testPM ( void )
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
    if ( in_data[PW_Start_Address+45+30*PW_j] > 0x40 )
    {
      return BAD;
    }
  }

  /* test 3 */
  if ( (in_data[PW_Start_Address+951] > 0x7F) && (in_data[PW_Start_Address+951] != 0xFF) )
  {
    return BAD;
  }

  return GOOD;
}


