#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM20 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+5458+PW_k*8]*256)+in_data[PW_Start_Address+5459+PW_k*8])*2);
  PW_j = (in_data[PW_Start_Address+5706]*256*256*256)+(in_data[PW_Start_Address+5707]*256*256)+(in_data[PW_Start_Address+5708]*256)+in_data[PW_Start_Address+5709];
  OutputSize = PW_WholeSampleSize + 5198 + PW_j;
  /*  printf ( "\b\b\b\b\b\b\b\bPromizer 2.0 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Promizer_20][0];
  OutName[2] = Extensions[Promizer_20][1];
  OutName[3] = Extensions[Promizer_20][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Promizer 2.0 module", Promizer_20 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}

