#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PRUN2 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+8+PW_k*8]*256)+in_data[PW_Start_Address+9+PW_k*8])*2);
  
  OutputSize = PW_j + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bProrunner 2 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[ProRunner_v2][0];
  OutName[2] = Extensions[ProRunner_v2][1];
  OutName[3] = Extensions[ProRunner_v2][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Prorunner 2 module", ProRunner_v2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* -1 should do but call it "just to be sure" :) */
}

