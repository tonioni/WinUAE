#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_WN ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+30*PW_k]*256)+in_data[PW_Start_Address+43+30*PW_k])*2);
  PW_j = in_data[PW_Start_Address+1083];
  OutputSize = PW_WholeSampleSize + (PW_j*1024) + 1084;
  /*  printf ( "\b\b\b\b\b\b\b\bWanton Packer module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Wanton_packer][0];
  OutName[2] = Extensions[Wanton_packer][1];
  OutName[3] = Extensions[Wanton_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Wanton Packer module", Wanton_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1082);  /* 1081 should do but call it "just to be sure" :) */
}
