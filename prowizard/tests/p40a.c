#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testP40A ( void )
{
  PW_Start_Address = PW_i;

  /* number of pattern (real) */
  PW_j = in_data[PW_Start_Address+4];
  if ( PW_j > 0x7f )
  {
    return BAD;
  }

  /* number of sample */
  PW_k = in_data[PW_Start_Address+6];
  if ( (PW_k > 0x1F) || (PW_k == 0) )
  {
    return BAD;
  }

  /* test volumes */
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+35+PW_l*16] > 0x40 )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    /* size */
    PW_o = (in_data[PW_Start_Address+24+PW_l*16]*256)+in_data[PW_Start_Address+25+PW_l*16];
    /* loop size */
    PW_n = (in_data[PW_Start_Address+30+PW_l*16]*256)+in_data[PW_Start_Address+31+PW_l*16];
    PW_o *= 2;
    PW_n *= 2;

    if ( (PW_o > 0xFFFF) ||
	 (PW_n > 0xFFFF) )
    {
      return BAD;
    }

    if ( PW_n > (PW_o+2) )
    {
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
    PW_WholeSampleSize = 0;
    return BAD;
  }

  /* PW_WholeSampleSize is the size of the sample data .. WRONG !! */
  /* PW_k is the number of samples */
  return GOOD;
}

