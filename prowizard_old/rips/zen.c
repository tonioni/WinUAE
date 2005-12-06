#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_ZEN ( void )
{
  /* PW_n is the highest sample data address */
  /* PW_WholeSampleSize if its size */

  OutputSize = PW_WholeSampleSize + PW_n;
  /*  printf ( "\b\b\b\b\b\b\b\bZEN Packer module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[ZEN][0];
  OutName[2] = Extensions[ZEN][1];
  OutName[3] = Extensions[ZEN][2];*/

  CONVERT = GOOD;
  Save_Rip ( "ZEN Packer module", ZEN );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 10);  /* 9 should do but call it "just to be sure" :) */
}
