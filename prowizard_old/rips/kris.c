/*
 * Update: 20/12/2000
 *  - debug .. correct size calculated now.
*/


#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_KRIS ( void )
{
  /*  OutName[1] = Extensions[KRIS_tracker][0];
  OutName[2] = Extensions[KRIS_tracker][1];
  OutName[3] = Extensions[KRIS_tracker][2];*/
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+44+PW_k*30]*256)+in_data[PW_Start_Address+45+PW_k*30])*2);
  PW_l = 0;
  for ( PW_k=0 ; PW_k<512 ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+958+PW_k*2]*256)+in_data[PW_Start_Address+959+PW_k*2] > PW_l )
      PW_l = (in_data[PW_Start_Address+958+PW_k*2]*256)+in_data[PW_Start_Address+959+PW_k*2];
  }
  PW_k = 1984 + PW_l + 256;
  OutputSize = PW_k + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bKRIS Tracker module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  
  CONVERT = GOOD;
  Save_Rip ( "KRIS Tracker module", KRIS_tracker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 954);  /* 953 should do but call it "just to be sure" :) */
}

