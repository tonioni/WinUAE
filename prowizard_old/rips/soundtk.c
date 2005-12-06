#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_SoundTracker ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is still the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 600;

  CONVERT = BAD;
  Save_Rip ( "SoundTracker module", SoundTracker );
  
  if ( Save_Status == GOOD )
    PW_i += 46;  /* after 1st volume */
}

