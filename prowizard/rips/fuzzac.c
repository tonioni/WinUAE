#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Fuzzac ( void )
{
  /* PW_WholeSampleSize IS still the whole sample size */

  PW_j = in_data[PW_Start_Address+2114];
  PW_k = in_data[PW_Start_Address+2115];
  OutputSize = PW_WholeSampleSize + (PW_j*16) + (PW_k*256) + 2118 + 4;
  /*  printf ( "\b\b\b\b\b\b\b\bFuzzac packer module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Fuzzac][0];
  OutName[2] = Extensions[Fuzzac][1];
  OutName[3] = Extensions[Fuzzac][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Fuzzac packer module", Fuzzac );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* -1 should do but call it "just to be sure" :) */
}

