#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_P60A ( void )
{
  /* PW_j is the number of sample */
  /* PW_WholeSampleSize is the whole sample data size */

  /*  OutName[1] = Extensions[P60A][0];
  OutName[2] = Extensions[P60A][1];
  OutName[3] = Extensions[P60A][2];*/

  OutputSize = PW_j + PW_WholeSampleSize;

  /*  printf ( "\b\b\b\b\b\b\b\bThe Player 6.0A module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "The Player 6.0A module", P60A );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 12);  /* 7 should do but call it "just to be sure" :) */
}

