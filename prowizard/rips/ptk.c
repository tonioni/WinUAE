#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PTK ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is still the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  CONVERT = BAD;
  Save_Rip ( "Protracker module", Protracker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}

