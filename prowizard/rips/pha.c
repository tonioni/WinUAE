#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PHA ( void )
{
  /* PW_k is still the highest pattern address ... so, 'all' we */
  /* have to do, here, is to depack the last pattern to get its */
  /* size ... that's all we need. */
  /* NOTE: we dont need to calculate the whole sample size, so */
  PW_m = 0;

  /*  printf ( "(pha)Where : %ld\n"
           "(pha)PW_Start_Address : %ld "
           "(pha)PW_k : %ld\n"
           , PW_i, PW_Start_Address, PW_k );
	   fflush (stdout);*/
  for ( PW_j=0 ; PW_j<256 ; PW_j++ )
  {
    /* 192 = 1100-0000 ($C0) */
    if ( in_data[PW_Start_Address+PW_k+PW_m] < 192 )
    {
      PW_m += 4;
      continue;
    }
    else
    {
      PW_l = 255 - in_data[PW_Start_Address+PW_k+PW_m+1];
      PW_m += 2;
      PW_j += (PW_l-1);
    }
  }
  OutputSize = PW_m + PW_k;

  /*  printf ( "\b\b\b\b\b\b\b\bPHA Packed music found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Pha_packer][0];
  OutName[2] = Extensions[Pha_packer][1];
  OutName[3] = Extensions[Pha_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "PHA Packed music", Pha_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 12);  /* 11 should do but call it "just to be sure" :) */
}

