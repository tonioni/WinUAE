#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PP10 ( void )
{
  /* PW_k is still the size of the track data */
  /* PW_WholeSampleSize is still the sample data size */

  OutputSize = PW_WholeSampleSize + PW_k + 762;

  /*  printf ( "\b\b\b\b\b\b\b\bProPacker v1.0 Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[PP10][0];
  OutName[2] = Extensions[PP10][1];
  OutName[3] = Extensions[PP10][2];*/

  CONVERT = GOOD;
  Save_Rip ( "ProPacker v1.0 Exe-file", PP10 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}

