#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_GPMO ( void )
{
  /* PW_k is still the nbr of pattern */

  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+in_data[PW_Start_Address+43+PW_j*30])*2);

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  /*  printf ( "\b\b\b\b\b\b\b\bProtracker mocule found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Protracker][0];
  OutName[2] = Extensions[Protracker][1];
  OutName[3] = Extensions[Protracker][2];*/

  CONVERT = BAD;
  Save_Rip ( "GPMO (Crunch Player) module", GPMO );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}

