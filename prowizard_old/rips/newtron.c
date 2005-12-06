#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Newtron ( void )
{
  /* PW_WholeSampleSize is the whole sample size :) */

  PW_k = (in_data[PW_Start_Address+2]*256) + in_data[PW_Start_Address+3];
  
  OutputSize = PW_k + PW_WholeSampleSize + 4;

  CONVERT = GOOD;
  Save_Rip ( "Newtron module", Newtron );
  
  if ( Save_Status == GOOD )
    PW_i += 7;
}

