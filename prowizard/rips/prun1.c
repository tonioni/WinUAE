#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PRUN1 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+30*PW_k]*256)+in_data[PW_Start_Address+43+30*PW_k])*2);
  PW_l=0;
  for ( PW_k=0 ; PW_k<128 ; PW_k++ )
    if ( in_data[PW_Start_Address+952+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+952+PW_k];
  PW_l += 1;
  OutputSize = (PW_l*1024) + 1084 + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bProrunner 1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[ProRunner_v1][0];
  OutName[2] = Extensions[ProRunner_v1][1];
  OutName[3] = Extensions[ProRunner_v1][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Prorunner 1 module", ProRunner_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1083); /* 1080 could be enough */
}

