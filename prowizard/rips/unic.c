#ifdef DOS
#include "../include/globals.h"
#include "../include/extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_UNIC_withID ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1084;

  /*  printf ( "\b\b\b\b\b\b\b\bUNIC tracker v1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[UNIC_v1][0];
  OutName[2] = Extensions[UNIC_v1][1];
  OutName[3] = Extensions[UNIC_v1][2];*/

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v1 module", UNIC_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}


void Rip_UNIC_noID ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1080;

  /*  printf ( "\b\b\b\b\b\b\b\bUNIC tracker v1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[UNIC_v1][0];
  OutName[2] = Extensions[UNIC_v1][1];
  OutName[3] = Extensions[UNIC_v1][2];*/

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v1 module", UNIC_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 46);  /* 45 should do but call it "just to be sure" :) */
}
