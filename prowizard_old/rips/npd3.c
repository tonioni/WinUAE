#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Noisepacker3 ( void )
{
  /*  OutName[1] = Extensions[Noisepacker3][0];
  OutName[2] = Extensions[Noisepacker3][1];
  OutName[3] = Extensions[Noisepacker3][2];*/

  OutputSize = PW_k + PW_WholeSampleSize + PW_l;
  /*  printf ( "\b\b\b\b\b\b\b\bNoisePacker v3 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "NoisePacker v3 module", Noisepacker3 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 16);  /* 15 should do but call it "just to be sure" :) */
}

