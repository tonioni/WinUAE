#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PP30 ( void )
{
  /* PW_k is still the size of the track "data" ! */
  /* PW_WholeSampleSize is still the whole sample size */

  PW_l = (in_data[PW_Start_Address+762+PW_k]*256*256*256)
    +(in_data[PW_Start_Address+763+PW_k]*256*256)
    +(in_data[PW_Start_Address+764+PW_k]*256)
    +in_data[PW_Start_Address+765+PW_k];

  OutputSize = PW_WholeSampleSize + PW_k + PW_l + 766;

  CONVERT = GOOD;
  Save_Rip ( "ProPacker v3.0 module", Propacker_30 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}

