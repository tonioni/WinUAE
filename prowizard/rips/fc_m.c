#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_FC_M ( void )
{
  /*  OutName[1] = Extensions[FC_M_packer][0];
  OutName[2] = Extensions[FC_M_packer][1];
  OutName[3] = Extensions[FC_M_packer][2];*/

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+34+PW_k*8]*256)+in_data[PW_Start_Address+35+PW_k*8])*2);
  PW_j = in_data[PW_Start_Address+286];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+292+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+292+PW_k];
  }
  PW_l += 1;
  OutputSize = (PW_l*1024) + PW_WholeSampleSize + 292 + PW_j + 8;
  /*  printf ( "\b\b\b\b\b\b\b\bFC-M packed module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "FC-M packed module", FC_M_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1);  /* 0 should do but call it "just to be sure" :) */
}

