/*
 * 27 dec 2001 : added some checks to prevent readings outside of
 * the input file.
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSTARPACK ( void )
{
  /* test 1 */
  if ( (PW_i < 23) || ((PW_i+269-23)>=PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-23;
  PW_l = (in_data[PW_Start_Address+268]*256)+in_data[PW_Start_Address+269];
  PW_k = PW_l/4;
  if ( (PW_k*4) != PW_l )
  {
/*printf ( "#2,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_k>127 )
  {
/*printf ( "#2,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k==0) || ((PW_Start_Address+784)>PW_in_size) )
  {
/*printf ( "#2,2 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  if ( in_data[PW_Start_Address+784] != 0 )
  {
/*printf ( "#3,-1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3  smp size < loop start + loop size ? */
  /* PW_l is still the size of the pattern list */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+20+PW_k*8]*256)+in_data[PW_Start_Address+21+PW_k*8])*2);
    PW_m = (((in_data[PW_Start_Address+24+PW_k*8]*256)+in_data[PW_Start_Address+25+PW_k*8])*2)
                        +(((in_data[PW_Start_Address+26+PW_k*8]*256)+in_data[PW_Start_Address+27+PW_k*8])*2);
    if ( (PW_j+2) < PW_m )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }

  /* test #4  finetunes & volumes */
  /* PW_l is still the size of the pattern list */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+22+PW_k*8]>0x0f) || (in_data[PW_Start_Address+23+PW_k*8]>0x40) )
    {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test #5  pattern addresses > sample address ? */
  /* PW_l is still the size of the pattern list */
  /* get sample data address */
  if ( (PW_Start_Address + 0x314) > PW_in_size )
  {
/*printf ( "#5,-1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_k gets address of sample data */
  PW_k = (in_data[PW_Start_Address+784]*256*256*256)
    +(in_data[PW_Start_Address+785]*256*256)
    +(in_data[PW_Start_Address+786]*256)
    +in_data[PW_Start_Address+787];
  if ( (PW_k+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_k < 788 )
  {
/*printf ( "#5,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_k is the address of the sample data */
  /* pattern addresses > sample address ? */
  for ( PW_j=0 ; PW_j<PW_l ; PW_j+=4 )
  {
    /* PW_m gets each pattern address */
    PW_m = (in_data[PW_Start_Address+272+PW_j]*256*256*256)
      +(in_data[PW_Start_Address+273+PW_j]*256*256)
      +(in_data[PW_Start_Address+274+PW_j]*256)
      +in_data[PW_Start_Address+275+PW_j];
    if ( PW_m > PW_k )
    {
/*printf ( "#5,2 (Start:%ld) (smp addy:%ld) (pat addy:%ld) (pat nbr:%ld) (max:%ld)\n"
         , PW_Start_Address 
         , PW_k
         , PW_m
         , (PW_j/4)
         , PW_l );*/
      return BAD;
    }
  }
  /* test last patterns of the pattern list == 0 ? */
  PW_j += 2;
  while ( PW_j<128 )
  {
    PW_m = (in_data[PW_Start_Address+272+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+273+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+274+PW_j*4]*256)
      +in_data[PW_Start_Address+275+PW_j*4];
    if ( PW_m != 0 )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 1;
  }


  /* test pattern data */
  /* PW_k is the address of the sample data */
  PW_j = PW_Start_Address + 788;
  /* PW_j points on pattern data */
/*printf ( "PW_j:%ld , PW_k:%ld\n" , PW_j , PW_k );*/
  while ( PW_j<(PW_k+PW_Start_Address-4) )
  {
    if ( in_data[PW_j] == 0x80 )
    {
      PW_j += 1;
      continue;
    }
    if ( in_data[PW_j] > 0x80 )
    {
/*printf ( "#6 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* empty row ? ... not possible ! */
    if ( (in_data[PW_j]   == 0x00) &&
         (in_data[PW_j+1] == 0x00) &&
         (in_data[PW_j+2] == 0x00) &&
         (in_data[PW_j+3] == 0x00) )
    {
/*printf ( "#6,0 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx = C .. arg > 64 ? */
    if ( ((in_data[PW_j+2]*0x0f)==0x0C) && (in_data[PW_j+3]>0x40) )
    {
/*printf ( "#6,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx = D .. arg > 64 ? */
    if ( ((in_data[PW_j+2]*0x0f)==0x0D) && (in_data[PW_j+3]>0x40) )
    {
/*printf ( "#6,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 4;
  }

  return GOOD;
}
