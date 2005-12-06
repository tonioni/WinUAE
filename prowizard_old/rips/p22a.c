#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_P22A ( void )
{
  /* PW_k is the number of sample */

  PW_l = ( (in_data[PW_Start_Address+16]*256*256*256) +
	   (in_data[PW_Start_Address+17]*256*256) +
	   (in_data[PW_Start_Address+18]*256) +
	   in_data[PW_Start_Address+19] );

  /* get whole sample size */
  /* starting from the highest addy and adding the sample size */
  PW_o = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_m = ( (in_data[PW_Start_Address+20+PW_j*16]*256*256*256) +
	     (in_data[PW_Start_Address+21+PW_j*16]*256*256) +
	     (in_data[PW_Start_Address+22+PW_j*16]*256) +
	     in_data[PW_Start_Address+23+PW_j*16] );
    if ( PW_m > PW_o )
    {
      PW_o = PW_m;
      PW_n = ( (in_data[PW_Start_Address+24+PW_j*16]*256) +
	       in_data[PW_Start_Address+25+PW_j*16] );
    }
  }

  OutputSize = PW_l + PW_o + (PW_n*2) + 4;

  CONVERT = GOOD;
  Save_Rip ( "The Player 2.2A module", ThePlayer22a );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}

