#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_SoundFX13 ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is the WholeSampleSize :) */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 0x294;

  /*printf ( "\b\b\b\b\b\b\b\bSound FX 1.3 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[SoundFX][0];
  OutName[2] = Extensions[SoundFX][1];
  OutName[3] = Extensions[SoundFX][2];*/

#ifdef UNIX
  CONVERT = BAD;
#else
  CONVERT = GOOD;
#endif
  Save_Rip ( "Sound FX 1.3 module", SoundFX );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 0x40);  /* 0x3C should do but call it "just to be sure" :) */
}

