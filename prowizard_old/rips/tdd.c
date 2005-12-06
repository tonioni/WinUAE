#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_TheDarkDemon ( void )
{
  /* PW_WholeSampleSize is the WholeSampleSize + pattern data size */

  /* 564 = header */
  OutputSize = PW_WholeSampleSize + 564;

  /*  printf ( "\b\b\b\b\b\b\b\bThe Dark Demon module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[TDD][0];
  OutName[2] = Extensions[TDD][1];
  OutName[3] = Extensions[TDD][2];*/

  CONVERT = GOOD;
  Save_Rip ( "The Dark Demon module", TDD );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 140);  /* 137 should do but call it "just to be sure" :) */
}
