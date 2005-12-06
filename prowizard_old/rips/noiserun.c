#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Noiserunner ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  /*  printf ( "\b\b\b\b\b\b\b\bNoiserunner music file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Noiserunner][0];
  OutName[2] = Extensions[Noiserunner][1];
  OutName[3] = Extensions[Noiserunner][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Noiserunner music", Noiserunner );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}

