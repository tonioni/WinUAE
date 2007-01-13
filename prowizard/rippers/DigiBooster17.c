/* testDigiBooster17() */
/* Rip_DigiBooster17() */

#include "globals.h"
#include "extern.h"


short testDigiBooster17 ( void )
{
  PW_Start_Address = PW_i;

  /* get nbr of pattern saved */
  PW_m = in_data[PW_Start_Address+46];  /*this value is -1 !*/

  /* test if there are pattern in pattern list > number of pattern saved */
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+48+PW_j] > PW_m )
    {
/*printf ( "#1 (Start:%ld) (max pat:%ld) (pat wrong:%d)\n"
         , PW_Start_Address , PW_m , in_data[PW_Start_Address+48+PW_j] );*/
       return BAD;
    }
  }

  /* samples > $FFFFFF ) */
  /* PW_m is the number of pattern saved -1 */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_o = ((in_data[PW_Start_Address+176+(PW_j*4)]*256*256*256)+
            (in_data[PW_Start_Address+177+(PW_j*4)]*256*256)+
            (in_data[PW_Start_Address+178+(PW_j*4)]*256)+
             in_data[PW_Start_Address+179+(PW_j*4)] );
    if ( PW_o > 0xFFFFFF )
    {
/*printf ( "#2 (Start:%ld) (sample:%ld) (len:%ld)\n"
         , PW_Start_Address , PW_j , PW_o );*/
      return BAD;
    }
    /* volumes */
    if ( in_data[PW_Start_Address+548+PW_j] > 0x40 )
    {
/*printf ( "#3 (Start:%ld) (sample:%ld) (vol:%d)\n"
         , PW_Start_Address , PW_j , in_data[PW_Start_Address+548+PW_j] );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
  }


  /* pattern sizes */
  /* PW_m is the number of pattern saved -1 */
  /* PW_WholeSampleSize is the whole sample size :) */
  PW_k = 1572;  /* first pattern addy */
  for ( PW_j=0 ; PW_j<=PW_m ; PW_j++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k]*256)+in_data[PW_Start_Address+PW_k+1];
    /* size < 64 ? */
    if ( PW_o < 64 )
    {
/*printf ( "#4 (Start:%ld) (pat size:%ld) (at:%ld)\n" , PW_Start_Address , PW_o , PW_k );*/
      return BAD;
    }
    PW_k += PW_o+2;
  }


  /* PW_m is the number of pattern saved -1 */
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_k is the module size saves the samples data */

  return GOOD;
}


void Rip_DigiBooster17 ( void )
{
  /* PW_m is the number of pattern saved -1 */
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_k is the module size saves the samples data */

  OutputSize = PW_WholeSampleSize + PW_k;

  CONVERT = BAD;
  Save_Rip ( "DigiBooster 1.7 module", DigiBooster );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}
