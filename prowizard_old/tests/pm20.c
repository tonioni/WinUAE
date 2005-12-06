#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPM2 ( void )
{
  PW_Start_Address = PW_i;
  /* test 1 */
  if ( (PW_Start_Address + 5714) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  /*if ( in_data[PW_Start_Address+5094] != 0x03 )*/
    /* not sure in fact ... */
    /* well, it IS the frequency table, it always seem */
    /* to be the 'standard one .. so here, there is 0358h */
  /*  {
    return BAD;
    }*/
  
  /* test 3 */
  if ( in_data[PW_Start_Address+5461] > 0x40 )
    /* testing a volume */
  {
    return BAD;
  }

  return GOOD;
}

