#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_XANN ( void )
{
  /* PW_WholeSampleSize is the whole sample size */

  PW_k=0;
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+2+PW_j*4];
    if ( PW_l > PW_k )
      PW_k = PW_l;
  }
  PW_k /= 4;
  OutputSize = PW_WholeSampleSize + 1084 + (PW_k*1024);

  CONVERT = GOOD;
  Save_Rip ( "Xann Packer module", XANN_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}
