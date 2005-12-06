#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_STK26 ( void )
{
  /* PW_WholeSampleSize is the whole sample siz */
  OutputSize = (in_data[PW_Start_Address+951]*256) + 1468 + PW_WholeSampleSize;

  CONVERT = GOOD;
  if ( in_data[PW_Start_Address+1464] == 'M' )
  {
    /*    OutName[1] = Extensions[STK26][0];
    OutName[2] = Extensions[STK26][1];
    OutName[3] = Extensions[STK26][2];*/
    Save_Rip ( "Sountracker 2.6 module", STK26 );
    /*    printf ( "\b\b\b\b\b\b\b\bSountracker 2.6 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  }
  else
  {
    /*    OutName[1] = Extensions[IceTracker][0];
    OutName[2] = Extensions[IceTracker][1];
    OutName[3] = Extensions[IceTracker][2];*/
    Save_Rip ( "IceTracker 1.0 modul", IceTracker );
    /*    printf ( "\b\b\b\b\b\b\b\bIceTracker 1.0 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  }

  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1470);  /* 1464 should do but call it "just to be sure" :) */
}

