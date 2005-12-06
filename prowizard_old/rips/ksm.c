#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_KSM ( void )
{
  /* PW_j is the highest track number */

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
    PW_WholeSampleSize += ((in_data[PW_Start_Address+52+PW_k*32]*256)+in_data[PW_Start_Address+53+PW_k*32]);
  
  OutputSize = ((PW_j+1)*192) + PW_WholeSampleSize + 1536;
  /*  printf ( "\b\b\b\b\b\b\b\bKefrens Sound Machine module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[KSM][0];
  OutName[2] = Extensions[KSM][1];
  OutName[3] = Extensions[KSM][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Kefrens Sound Machine module", KSM );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* -1 should do but call it "just to be sure" :) */
}

