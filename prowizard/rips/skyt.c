#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_SKYT ( void )
{
  /* PW_WholeSampleSize is still the whole sample size */

  PW_l=0;
  PW_j = in_data[PW_Start_Address+260]+1;
  for ( PW_k=0 ; PW_k<(PW_j*4) ; PW_k++ )
  {
    PW_m = (in_data[PW_Start_Address+262+PW_k*2]);
    if ( PW_m > PW_l )
    {
      PW_l = PW_m;
    }
    /*printf ( "[%ld]:%ld\n",PW_k,PW_m);*/
  }
  OutputSize = (PW_l*256) + 262 + PW_WholeSampleSize + (PW_j*8);
  /*printf ( "\b\b\b\b\b\b\b\bSKYT Packed module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[SKYT_packer][0];
  OutName[2] = Extensions[SKYT_packer][1];
  OutName[3] = Extensions[SKYT_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "SKYT Packed module", SKYT_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 261); /* 260 could be enough */
}

