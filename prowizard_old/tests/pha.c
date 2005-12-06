#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPHA ( void )
{
  /* test #1 */
  if ( PW_i < 11 )
  {
/*
printf ( "#1 (PW_i:%ld)\n" , PW_i );
*/
    return BAD;
  }

  /* test #2 (volumes,sample addresses and whole sample size) */
  PW_Start_Address = PW_i-11;
  PW_l=0;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* sample size */
    PW_n =(((in_data[PW_Start_Address+PW_j*14]*256)+in_data[PW_Start_Address+PW_j*14+1])*2);
    PW_WholeSampleSize += PW_n;
    /* loop start */
    PW_m =(((in_data[PW_Start_Address+PW_j*14+4]*256)+in_data[PW_Start_Address+PW_j*14+5])*2);

    if ( in_data[PW_Start_Address+3+PW_j*14] > 0x40 )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_m > PW_WholeSampleSize )
    {
/*printf ( "#2,1 (start:%ld) (smp nbr:%ld) (size:%ld) (lstart:%ld)\n"
         , PW_Start_Address,PW_j,PW_n,PW_m );*/
      return BAD;
    }
    PW_k = (in_data[PW_Start_Address+8+PW_j*14]*256*256*256)
         +(in_data[PW_Start_Address+9+PW_j*14]*256*256)
         +(in_data[PW_Start_Address+10+PW_j*14]*256)
         +in_data[PW_Start_Address+11+PW_j*14];
    /* PW_k is the address of this sample data */
    if ( (PW_k < 0x3C0) || (PW_k>PW_in_size) ) 
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( (PW_WholeSampleSize <= 2) || (PW_WholeSampleSize>(31*65535)) )
  {
    /*printf ( "#2,3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the WholeSampleSize */
  PW_l = PW_WholeSampleSize + 960;
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    PW_m = (in_data[PW_Start_Address+448+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+449+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+450+PW_j*4]*256)
      +in_data[PW_Start_Address+451+PW_j*4];
    if ( PW_m > PW_k )
      PW_k = PW_m;
    if ( (PW_m+2) < PW_l )
    {
      /*printf ( "#5 (start:%ld)(add:%ld)(min:%ld)(where:%ld)\n" , PW_Start_Address,PW_m,PW_l, PW_j );*/
      return BAD;
    }
  }
  /* PW_k is the highest pattern data address */


  return GOOD;
}


