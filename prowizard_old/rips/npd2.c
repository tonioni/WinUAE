#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Noisepacker2 ( void )
{
  /*  OutName[1] = Extensions[Noisepacker2][0];
  OutName[2] = Extensions[Noisepacker2][1];
  OutName[3] = Extensions[Noisepacker2][2];*/

  OutputSize = PW_k + PW_WholeSampleSize + PW_l;
  /*  printf ( "\b\b\b\b\b\b\b\bNoisePacker v2 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "NoisePacker v2 module", Noisepacker2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 16);  /* 15 should do but call it "just to be sure" :) */
}

