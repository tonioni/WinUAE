#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_HEATSEEKER ( void )
{
  /*  OutName[1] = Extensions[Heatseeker][0];
  OutName[2] = Extensions[Heatseeker][1];
  OutName[3] = Extensions[Heatseeker][2];*/

  OutputSize = PW_k + PW_WholeSampleSize + 378;
  /*  printf ( "\b\b\b\b\b\b\b\bHeatseeker module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "Heatseeker module", Heatseeker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}

