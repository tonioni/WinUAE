#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_MP_noID ( void )
{
  /*PW_WholeSampleSize is the whole sample size*/

  PW_j = in_data[PW_Start_Address+248];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
    if ( in_data[PW_Start_Address+250+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+250+PW_k];

  PW_k = (in_data[PW_Start_Address+378]*256*256*256)+
    (in_data[PW_Start_Address+379]*256*256)+
    (in_data[PW_Start_Address+380]*256)+
    in_data[PW_Start_Address+381];

  PW_l += 1;
  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 378;
  if ( PW_k == 0 )
    OutputSize += 4;
  /*  printf ( "\b\b\b\b\b\b\b\bModule Protector Packed music found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Module_protector][0];
  OutName[2] = Extensions[Module_protector][1];
  OutName[3] = Extensions[Module_protector][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Module Protector Packed music", Module_protector );
  
  if ( Save_Status == GOOD )
    PW_i += 0x57E;
  /*PW_i += (OutputSize - 5);  -- 4 should do but call it "just to be sure" :) */
}


void Rip_MP_withID ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2);
  PW_j = in_data[PW_Start_Address+252];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
    if ( in_data[PW_Start_Address+254+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+254+PW_k];

  PW_k = (in_data[PW_Start_Address+382]*256*256*256)+
    (in_data[PW_Start_Address+383]*256*256)+
    (in_data[PW_Start_Address+384]*256)+
    in_data[PW_Start_Address+385];

  PW_l += 1;
  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 382;

  /* not sure for the following test because I've never found */
  /* any MP file with "TRK1" ID. I'm basing all this on Gryzor's */
  /* statement in his Hex-dump exemple ... */
  if ( PW_k == 0 )
    OutputSize += 4;
  /*  printf ( "\b\b\b\b\b\b\b\bModule Protector Packed music found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Module_protector][0];
  OutName[2] = Extensions[Module_protector][1];
  OutName[3] = Extensions[Module_protector][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Module Protector Packed music", Module_protector );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 5);  /* 4 should do but call it "just to be sure" :) */
}

