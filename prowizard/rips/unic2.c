#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_UNIC2 ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1060;

  /*  printf ( "\b\b\b\b\b\b\b\bUNIC tracker v2 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[UNIC_v2][0];
  OutName[2] = Extensions[UNIC_v2][1];
  OutName[3] = Extensions[UNIC_v2][2];*/

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v2 module", UNIC_v2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 26);  /* 25 should do but call it "just to be sure" :) */
}
