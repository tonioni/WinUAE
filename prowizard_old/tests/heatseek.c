/*  (May 2002)
 *    added test_smps()
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testHEATSEEKER ( void )
{
  int nbr_notes=0;

  if ( (PW_i < 3) || ((PW_i+375)>=PW_in_size))
  {
    return BAD;
  }
  PW_Start_Address = PW_i-3;

  /* size of the pattern table */
  if ( (in_data[PW_Start_Address+248] > 0x7f) ||
       (in_data[PW_Start_Address+248] == 0x00) )
  {
    return BAD;
  }

  /* test noisetracker byte */
  if ( in_data[PW_Start_Address+249] != 0x7f )
  {
    return BAD;
  }

  /* test samples */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (in_data[PW_Start_Address+PW_k*8]*256)+in_data[PW_Start_Address+1+PW_k*8];
    /* loop start */
    PW_m = (in_data[PW_Start_Address+PW_k*8+4]*256)+in_data[PW_Start_Address+5+PW_k*8];
    /* loop size */
    PW_n = (in_data[PW_Start_Address+PW_k*8+6]*256)+in_data[PW_Start_Address+7+PW_k*8];
    PW_j *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( test_smps(PW_j, PW_m, PW_n, in_data[PW_Start_Address+3+PW_k*8], in_data[PW_Start_Address+2+PW_k*8] ) == BAD )
      return BAD;
    if ( (PW_j > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern table */
  PW_l = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j ++ )
  {
    if ( in_data[PW_Start_Address+250+PW_j] > 0x7f )
    {
      return BAD;
    }
    if ( in_data[PW_Start_Address+250+PW_j] > PW_l )
      PW_l = in_data[PW_Start_Address+250+PW_j];
  }
  /* PW_l = highest pattern number */
  if ( (PW_Start_Address + (PW_l*16) + 378) > PW_in_size )
  {
    /* PW_l*16 is the minimum size of all patterns */
    return BAD;
  }

  /* test notes */
  PW_k=0;
  PW_j=0;
  for ( PW_m=0 ; PW_m<=PW_l ; PW_m++ )
  {
    for ( PW_n=0 ; PW_n<4 ; PW_n++ )
    {
      for ( PW_o=0 ; PW_o<64 ; PW_o++ )
      {
	switch (in_data[PW_Start_Address+378+PW_j]&0xE0)
	{
	  case 0x00:
	    if ( ((in_data[PW_Start_Address+378+PW_j]&0x0F)>0x03) || (((in_data[PW_Start_Address+378+PW_j]&0x0F)==0x00) && (in_data[PW_Start_Address+379+PW_j]<0x71) && (in_data[PW_Start_Address+379+PW_j]!=0x00)))
	    {
	      /*printf ( "#6) start:%ld (%x) (at:%x) (PW_l:%ld)\n",PW_Start_Address, in_data[PW_Start_Address+378+PW_j], PW_Start_Address+378+PW_j,PW_l );*/
	      return BAD;
	    }
	    if ( ((in_data[PW_Start_Address+380+PW_j]&0x0f) == 0x0d) && (in_data[PW_Start_Address+381+PW_j]>0x64))
	    {
	      /*	      printf ( "#6,5 Start:%ld cmd D arg : %x\n", PW_Start_Address, in_data[PW_Start_Address+381+PW_j] );*/
	      return BAD;
	    }
	    PW_k += 4;
	    PW_j += 4;
	    if ( ((in_data[PW_Start_Address+378+PW_j]&0x0f)!=0x00) || (in_data[PW_Start_Address+379+PW_j]!=0x00))
	      nbr_notes = 1;
	    break;
	  case 0x80:
	    if (( in_data[PW_Start_Address+379+PW_j]!=0x00 ) || ( in_data[PW_Start_Address+380+PW_j]!=0x00 ))
	    {
/*printf ( "#7) start:%ld (%x) (at:%x)\n"
,PW_Start_Address, in_data[PW_Start_Address+379+PW_j], PW_Start_Address+379+PW_j );*/
	      return BAD;
	    }
	    PW_o += in_data[PW_Start_Address+381+PW_j];
	    PW_j += 4;
	    PW_k += 4;
	    break;
	  case 0xC0:
	    if ( in_data[PW_Start_Address+379+PW_j]!=0x00 )
	    {
/*printf ( "#7) start:%ld (%x) (at:%x)\n"
,PW_Start_Address, in_data[PW_Start_Address+379+PW_j], PW_Start_Address+379+PW_j );*/
	      return BAD;
	    }
	    PW_o = 100;
	    PW_j += 4;
	    PW_k += 4;
	    break;
	  default:
	    return BAD;
	    break;
	}
      }
    }
  }
  if ( nbr_notes == 0 )
  {
    /* only empty notes */
    return BAD;
  }

  /* PW_k is the size of the pattern data */
  /* PW_WholeSampleSize is the size of the sample data */
  return GOOD;
}
