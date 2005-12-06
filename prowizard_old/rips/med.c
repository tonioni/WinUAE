#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_MMD0 ( void )
{
  /* PW_k is the module size */

  OutputSize = PW_k;
  /*  if ( in_data[PW_i+3] == '0' )
    printf ( "\b\b\b\b\b\b\b\bMED (MMD0) module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );
  if ( in_data[PW_i+3] == '1' )
  printf ( "\b\b\b\b\b\b\b\bOctaMED (MMD1) module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[MED][0];
  OutName[2] = Extensions[MED][1];
  OutName[3] = Extensions[MED][2];*/

  CONVERT = BAD;
  if ( in_data[PW_i+3] == '0' )
    Save_Rip ( "MED (MMD0) module", MED );
  if ( in_data[PW_i+3] == '1' )
    Save_Rip ( "OctaMED (MMD1) module", MED );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}

