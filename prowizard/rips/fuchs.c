#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_FuchsTracker ( void )
{
  /* PW_m is the size of all samples (in descriptions) */
  /* PW_k is the pattern data size */

  /* 204 = 200 (header) + 4 ("INST" id) */
/*printf ( "sample size    : %ld\n" , PW_m );*/
/*printf ( "patt data size : %ld\n" , PW_k );*/
  OutputSize = PW_m + PW_k + 204;

  /*  printf ( "\b\b\b\b\b\b\b\bFuchs Tracker module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[FuchsTracker][0];
  OutName[2] = Extensions[FuchsTracker][1];
  OutName[3] = Extensions[FuchsTracker][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Fuchs Tracker module", FuchsTracker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 195);  /* 192 should do but call it "just to be sure" :) */
}

