#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_P50A ( void )
{
  /* PW_j is the number of sample */
  /* PW_WholeSampleSize is the whole sample data size */

  /*  OutName[1] = Extensions[P50A][0];
  OutName[2] = Extensions[P50A][1];
  OutName[3] = Extensions[P50A][2];*/

  OutputSize = PW_j + PW_WholeSampleSize;

  /*  printf ( "\b\b\b\b\b\b\b\bThe Player 5.0A module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "The Player 5.0A module", P50A );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 8);  /* 7 should do but call it "just to be sure" :) */
}

