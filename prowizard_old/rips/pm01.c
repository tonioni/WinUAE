#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM01 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);

  PW_k = (in_data[PW_Start_Address+762]*256*256*256)
    +(in_data[PW_Start_Address+763]*256*256)
    +(in_data[PW_Start_Address+764]*256)
    +in_data[PW_Start_Address+765];

  OutputSize = PW_WholeSampleSize + PW_k + 766;

  /*  printf ( "\b\b\b\b\b\b\b\bPromizer 0.1 music found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Promizer_01][0];
  OutName[2] = Extensions[Promizer_01][1];
  OutName[3] = Extensions[Promizer_01][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Promizer 0.1 music", Promizer_01 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}

