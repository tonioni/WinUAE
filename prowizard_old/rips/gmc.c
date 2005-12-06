#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_GMC ( void )
{
  /* PW_l is still the number of pattern to play */
  /* PW_WholeSampleSize is already the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 444;

  CONVERT = GOOD;
  Save_Rip ( "Game Music Creator module", GMC );
  
  if ( Save_Status == GOOD )
    PW_i += 444; /* after header */
}

