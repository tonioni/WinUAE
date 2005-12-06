#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_P61A ( void )
{
  /* PW_j is the number of sample */
  /* PW_WholeSampleSize is the whole sample data size */

  /*  OutName[1] = Extensions[P61A][0];
  OutName[2] = Extensions[P61A][1];
  OutName[3] = Extensions[P61A][2];*/

  OutputSize = PW_j + PW_WholeSampleSize;

  /*  printf ( "\b\b\b\b\b\b\b\bThe Player 6.1A module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "The Player 6.1A module", P61A );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 12);  /* 7 should do but call it "just to be sure" :) */
}

