#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_TP1 ( void )
{
  /* PW_WholeSampleSize is the size of the module :) */

  OutputSize = PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bTracker Packer v1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[TP1][0];
  OutName[2] = Extensions[TP1][1];
  OutName[3] = Extensions[TP1][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Tracker Packer v1 module", TP1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1); /* 0 could be enough */
}
