#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_BP ( void )
{
  OutputSize += ((PW_j*48) + (PW_l*16) + 512);
  /*  printf ( "\b\b\b\b\b\b\b\bSound Monitor v2 / v3 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[SoundMonitor][0];
  OutName[2] = Extensions[SoundMonitor][1];
  OutName[3] = Extensions[SoundMonitor][2];*/

  CONVERT = BAD;
  Save_Rip ( "Sound Monitor v2 / v3 module", SoundMonitor );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 27);  /* 26 should do but call it "just to be sure" :) */
}

