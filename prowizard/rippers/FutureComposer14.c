/*
 * update 18 mar 2003
 *  - fake test replaced by good test :) -> more FC14 found
*/
/* testFC14() */
/* Rip_FC14() */


#include "globals.h"
#include "extern.h"

short testFC14 ( void )
{
  PW_Start_Address = PW_i;

  /* file size < 193 */
  if ( (PW_in_size - PW_Start_Address) < 193 )
  {
/*printf ( "#1 (start:%ld) (size:%ld)\n" , PW_Start_Address , PW_in_size-PW_Start_Address);*/
    return BAD;
  }

  /* get 1st sample addy */
  PW_m = (( in_data[PW_Start_Address+32]*256*256*256)+
          ( in_data[PW_Start_Address+33]*256*256)+ 
          ( in_data[PW_Start_Address+34]*256)+
            in_data[PW_Start_Address+35] );

  /* test in-size again */
  if ( PW_Start_Address+PW_m > PW_in_size )
  {
/*printf ( "#2 (start:%ld) (1st smp addy:%ld)\n" , PW_Start_Address , PW_m);*/
    return BAD;
  }

  /* test various addresses  */
  PW_j = (( in_data[PW_Start_Address+8] *256*256*256)+
          ( in_data[PW_Start_Address+9] *256*256)+ 
          ( in_data[PW_Start_Address+10]*256)+
            in_data[PW_Start_Address+11] );

  PW_k = (( in_data[PW_Start_Address+16]*256*256*256)+
          ( in_data[PW_Start_Address+17]*256*256)+ 
          ( in_data[PW_Start_Address+18]*256)+
            in_data[PW_Start_Address+19] );

  PW_l = (( in_data[PW_Start_Address+36]*256*256*256)+
          ( in_data[PW_Start_Address+37]*256*256)+ 
          ( in_data[PW_Start_Address+38]*256)+
            in_data[PW_Start_Address+39] );

  /* test in-size again */
  if ( (PW_j > PW_in_size) || (PW_k > PW_in_size) || (PW_l > PW_in_size) )
  {
/*printf ( "#2 (start:%ld) (PW_j:%ld) (PW_k:%ld) (PW_l:%ld)\n"
           , PW_Start_Address , PW_j, PW_k, PW_l);*/
    return BAD;
  }

  /* PW_l is 1st waveform addy */
  return GOOD;
}


void Rip_FC14 ( void )
{
  /* PW_l is already the 1st waveform addy */

  /* get Waveforms len */
  PW_k = 0;
  for ( PW_j=100 ; PW_j<180 ; PW_j++ )
  {
    PW_k += in_data[PW_Start_Address+PW_j];
  }

  OutputSize = PW_l + (PW_k*2);

  CONVERT = BAD;
  Save_Rip ( "Future Composer 1.4 module", FC14 );
  
  if ( Save_Status == GOOD )
    PW_i += 4; /* after FC14 tag */
}

