#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testGMC ( void )
{
  /* test #1 */
  if ( (PW_i<7) || ((PW_Start_Address+444)>PW_in_size) )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-7;

  /* samples descriptions */
  PW_WholeSampleSize=0;
  PW_j=0;
  for ( PW_k = 0 ; PW_k < 15 ; PW_k ++ )
  {
    PW_o = (in_data[PW_Start_Address+16*PW_k+4]*256)+in_data[PW_Start_Address+16*PW_k+5];
    PW_n = (in_data[PW_Start_Address+16*PW_k+12]*256)+in_data[PW_Start_Address+16*PW_k+13];
    PW_o *= 2;
    /* volumes */
    if ( in_data[PW_Start_Address + 7 + (16*PW_k)] > 0x40 )
    {
/*printf ( "#2\n" );*/
      return BAD;
    }
    /* size */
    if ( PW_o > 0xFFFF )
    {
/*printf ( "#2,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_n > PW_o )
    {
/*printf ( "#2,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
    if ( PW_o != 0 )
      PW_j = PW_k+1;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#2,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the highest not null sample */

  /* pattern table size */
  if ( ( in_data[PW_Start_Address+243] > 0x64 ) ||
       ( in_data[PW_Start_Address+243] == 0x00 ) )
  {
    return BAD;
  }

  /* pattern order table */
  PW_l=0;
  for ( PW_n=0 ; PW_n<100 ; PW_n++ )
  {
    PW_k = ((in_data[PW_Start_Address+244+PW_n*2]*256)+
	    in_data[PW_Start_Address+245+PW_n*2]);
    if ( ((PW_k/1024)*1024) != PW_k )
    {
/*printf ( "#4 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
      return BAD;
    }
    PW_l = ((PW_k/1024)>PW_l) ? PW_k/1024 : PW_l;
  }
  PW_l += 1;
  /* PW_l is the number of pattern */
  if ( (PW_l == 1) || (PW_l >0x64) )
  {
    return BAD;
  }

  /* test pattern data */
  PW_o = in_data[PW_Start_Address+243];
  PW_m = 0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    for ( PW_n=0 ; PW_n<256 ; PW_n++ )
    {
      if ( ( in_data[PW_Start_Address+444+PW_k*1024+PW_n*4] > 0x03 ) ||
	   ( (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) >= 0x90 ))
      {
/*printf ( "#5,0 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) > PW_j )
      {
/*printf ( "#5,1 Start:%ld (PW_j:%ld) (where:%ld) (value:%x)\n"
         , PW_Start_Address , PW_j , PW_Start_Address+444+PW_k*1024+PW_n*4+2
         , ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 3) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x40) )
      {
/*printf ( "#5,2 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 4) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x63) )
      {
/*printf ( "#5,3 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 5) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > PW_o+1) )
      {
/*printf ( "#5,4 Start:%ld (effect:5)(PW_o:%ld)(4th note byte:%x)\n" , PW_Start_Address , PW_j , in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3]);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 6) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,5 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 7) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,6 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4]&0x0f) > 0x00) || (in_data[PW_Start_Address+445+PW_k*1024+PW_n*4] > 0x00) )
	PW_m = 1;
    }
  }
  if ( PW_m == 0 )
  {
    /* only empty notes */
    return BAD;
  }
  /* PW_WholeSampleSize is the whole sample size */

  return GOOD;
}

