#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_AC1D ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+8+PW_j*8]*256)+in_data[PW_Start_Address+9+PW_j*8])*2);
  PW_k = (in_data[PW_Start_Address+4]*256*256*256)+
    (in_data[PW_Start_Address+5]*256*256)+
    (in_data[PW_Start_Address+6]*256)+
    in_data[PW_Start_Address+7];

  OutputSize = PW_WholeSampleSize + PW_k;
  /*  printf ( "\b\b\b\b\b\b\b\bAC1D Packed module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[AC1D_packer][0];
  OutName[2] = Extensions[AC1D_packer][1];
  OutName[3] = Extensions[AC1D_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "AC1D Packed module", AC1D_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}

