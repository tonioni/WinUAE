#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM ( void )
{
  /*  OutName[1] = Extensions[Power_Music][0];
  OutName[2] = Extensions[Power_Music][1];
  OutName[3] = Extensions[Power_Music][2];*/

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);

  PW_j = in_data[PW_Start_Address+950];
  PW_l=0;
  for ( PW_k=0 ; PW_k<128 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+952+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+952+PW_k];
  }
  PW_l += 1;
  PW_k = 1084 + (PW_l * 1024);
  OutputSize = PW_k + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bPower Music module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "Power Music module", Power_Music );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1083);  /* 1080 should do but call it "just to be sure" :) */
}

