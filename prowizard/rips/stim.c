#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_STIM ( void )
{
  OutputSize = PW_WholeSampleSize + PW_j + 31*4 + 31*8;

  /*  printf ( "\b\b\b\b\b\b\b\bSTIM (Slamtilt) module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[STIM][0];
  OutName[2] = Extensions[STIM][1];
  OutName[3] = Extensions[STIM][2];*/

  CONVERT = GOOD;
  Save_Rip ( "STIM (Slamtilt) module", STIM );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1);  /* 0 should do but call it "just to be sure" :) */
}

