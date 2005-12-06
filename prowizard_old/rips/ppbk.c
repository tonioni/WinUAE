#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PPbk ( void )
{
  /* PW_l is still the whole size */

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bAMOS PowerPacker Bank \"PPbk\" Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  printf ( "  extracting PP20 subfile ...\n" );
  /*  OutName[1] = Extensions[PP20][0];
  OutName[2] = Extensions[PP20][1];
  OutName[3] = Extensions[PP20][2];*/

  CONVERT = BAD;

  PW_Start_Address += 16;
  Save_Rip ( "AMOS PowerPacker Bank \"PPbk\" Data-file", PP20 );

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 40);  /* 36 should do but call it "just to be sure" :) */
}
