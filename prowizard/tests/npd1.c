#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testNoisepacker1 ( void )
{
  if ( PW_i < 15 )
  {
    return BAD;
  }
  PW_Start_Address = PW_i-15;

  /* size of the pattern table */
  PW_j = (in_data[PW_Start_Address+2]*256)+in_data[PW_Start_Address+3];
  if ( (((PW_j/2)*2) != PW_j) || (PW_j == 0) )
  {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the size of the pattern list (*2) */

  /* test nbr of samples */
  if ( (in_data[PW_Start_Address+1]&0x0f) != 0x0C )
  {
/*printf ( "#3,0 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_l = ((in_data[PW_Start_Address]<<4)&0xf0)|((in_data[PW_Start_Address+1]>>4)&0x0f);
  if ( (PW_l > 0x1F) || (PW_l == 0) || ((PW_Start_Address+PW_j+8+PW_l*8)>PW_in_size))
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l is the number of samples */

  /* test volumes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+15+PW_k*16] > 0x40 )
    {
/*printf ( "#3,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize=0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k*16+12]*256)+in_data[PW_Start_Address+PW_k*16+13];
    PW_m = (in_data[PW_Start_Address+PW_k*16+20]*256)+in_data[PW_Start_Address+PW_k*16+21];
    PW_n = (in_data[PW_Start_Address+PW_k*16+22]*256)+in_data[PW_Start_Address+PW_k*16+23];
    PW_o *= 2;
    PW_m *= 2;
    if ( (PW_o > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m + PW_n) > (PW_o+2) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_n != 0) && (PW_m == 0) )
    {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }


  /* small shit to gain some vars */
  PW_l *= 16;
  PW_l += 8;
  PW_l += 4;
  /* PW_l is now the size of the header 'til the end of sample descriptions */
  if (PW_l+PW_Start_Address > PW_in_size )
  {
    return BAD;
  }


  /* test pattern table */
  PW_n=0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k += 2 )
  {
    PW_m = ((in_data[PW_Start_Address+PW_l+PW_k]*256)+in_data[PW_Start_Address+PW_l+PW_k+1]);
    if ( ((PW_m/8)*8) != PW_m )
    {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_m > PW_n )
      PW_n = PW_m;
  }
  PW_l += PW_j;
  PW_l += PW_n;
  PW_l += 8; /*paske on a que l'address du dernier pattern .. */
  /* PW_l is now the size of the header 'til the end of the track list */
  /* PW_j is now available for use :) */

  /* test track data size */
  PW_k = (in_data[PW_Start_Address+6]*256)+in_data[PW_Start_Address+7];
  if ( (PW_k < 192) || (((PW_k/192)*192) != PW_k) )
  {
    /*printf ( "#7 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test notes */
  for ( PW_m=0 ; PW_m < PW_k ; PW_m+=3 )
  {
    if ( PW_Start_Address + PW_l + PW_m > PW_in_size )
    {
/*printf ( "#8,0 Start:%ld\n", PW_Start_Address );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_l+PW_m] > 0x49 )
    {
      /*printf ( "#8 Start:%ld (at %x)(PW_k:%x)(PW_l:%x)(PW_m:%x)\n" , PW_Start_Address,PW_Start_Address+PW_l+PW_m,PW_k,PW_l,PW_m );*/
      return BAD;
    }
  }

  /* PW_WholeSampleSize is the size of the sample data */
  /* PW_l is the size of the header 'til the track datas */
  /* PW_k is the size of the track datas */
  return GOOD;
}
