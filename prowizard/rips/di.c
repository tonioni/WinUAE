#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_DI ( void )
{
  /*PW_WholeSampleSize is already the whole sample size */

  PW_j = (in_data[PW_Start_Address+10]*256*256*256)
        +(in_data[PW_Start_Address+11]*256*256)
        +(in_data[PW_Start_Address+12]*256)
        +in_data[PW_Start_Address+13];

  OutputSize = PW_WholeSampleSize + PW_j;

  CONVERT = GOOD;
  Save_Rip ( "Digital Illusion Packed music", Digital_illusion );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 15);  /* 14 should do but call it "just to be sure" :) */
}
