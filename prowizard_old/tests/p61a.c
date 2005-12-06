#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testP61A_nopack ( void )
{
  if ( PW_i < 7 )
  {
    return BAD;
  }
  PW_Start_Address = PW_i-7;

  /* number of pattern (real) */
  PW_m = in_data[PW_Start_Address+2];
  if ( (PW_m > 0x7f) || (PW_m == 0) )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_m is the real number of pattern */

  /* number of sample */
  PW_k = (in_data[PW_Start_Address+3]&0x3F);
  if ( (PW_k > 0x1F) || (PW_k == 0) || ((PW_k*6+PW_Start_Address+7)>=PW_in_size))
  {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_k is the number of sample */

  /* test volumes */
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+7+PW_l*6] > 0x40 )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test fines */
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+6+PW_l*6] > 0x0F )
    {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes and loop start */
  PW_WholeSampleSize = 0;
  for ( PW_n=0 ; PW_n<PW_k ; PW_n++ )
  {
    PW_o = ( (in_data[PW_Start_Address+4+PW_n*6]*256) +
	     in_data[PW_Start_Address+5+PW_n*6] );
    if ( ((PW_o < 0xFFDF) && (PW_o > 0x8000)) || (PW_o == 0) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_o < 0xFF00 )
      PW_WholeSampleSize += (PW_o*2);

    PW_j = ( (in_data[PW_Start_Address+8+PW_n*6]*256) +
	     in_data[PW_Start_Address+9+PW_n*6] );
    if ( (PW_j != 0xFFFF) && (PW_j >= PW_o) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_o > 0xFFDF )
    {
      if ( (0xFFFF-PW_o) > PW_k )
      {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
        return BAD;
      }
    }
  }

  /* test sample data address */
  PW_j = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1];
  if ( PW_j < (PW_k*6+4+PW_m*8) )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the address of the sample data */


  /* test track table */
  for ( PW_l=0 ; PW_l<(PW_m*4) ; PW_l++ )
  {
    PW_o = ((in_data[PW_Start_Address+4+PW_k*6+PW_l*2]*256)+
            in_data[PW_Start_Address+4+PW_k*6+PW_l*2+1] );
    if ( (PW_o+PW_k*6+4+PW_m*8) > PW_j )
    {
/*printf ( "#7 Start:%ld (value:%ld)(where:%x)(PW_l:%ld)(PW_m:%ld)(PW_o:%ld)\n"
, PW_Start_Address
, (in_data[PW_Start_Address+PW_k*6+4+PW_l*2]*256)+in_data[PW_Start_Address+4+PW_k*6+PW_l*2+1]
, PW_Start_Address+PW_k*6+4+PW_l*2
, PW_l
, PW_m
, PW_o );*/
      return BAD;
    }
  }

  /* test pattern table */
  PW_l=0;
  PW_o=0;
  /* first, test if we dont oversize the input file */
  if ( (PW_Start_Address+PW_k*6+4+PW_m*8) > PW_in_size )
  {
/*printf ( "8,0 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  while ( (in_data[PW_Start_Address+PW_k*6+4+PW_m*8+PW_l] != 0xFF) && (PW_l<128) )
  {
    if ( in_data[PW_Start_Address+PW_k*6+4+PW_m*8+PW_l] > (PW_m-1) )
    {
/*printf ( "#8,1 Start:%ld (value:%ld)(where:%x)(PW_l:%ld)(PW_m:%ld)(PW_k:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_k*6+4+PW_m*8+PW_l]
, PW_Start_Address+PW_k*6+4+PW_m*8+PW_l
, PW_l
, PW_m
, PW_k );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_k*6+4+PW_m*8+PW_l] > PW_o )
      PW_o = in_data[PW_Start_Address+PW_k*6+4+PW_m*8+PW_l];
    PW_l++;
  }
  /* are we beside the sample data address ? */
  if ( (PW_k*6+4+PW_m*8+PW_l) > PW_j )
  {
    return BAD;
  }
  if ( (PW_l == 0) || (PW_l == 128) )
  {
/*printf ( "#8.2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_o += 1;
  /* PW_o is the highest number of pattern */


  /* test notes ... pfiew */
  PW_l += 1;
  for ( PW_n=(PW_k*6+4+PW_m*8+PW_l) ; PW_n<PW_j ; PW_n++ )
  {
    if ( (in_data[PW_Start_Address+PW_n]&0xff) == 0xff )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0x00 )
      {
	PW_n += 1;
	continue;
      }
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0x40 )
      {
	PW_n += 2;
	continue;
      }
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0xc0 )
      {
	if ( PW_n < ((in_data[PW_Start_Address+PW_n+2]*256)+in_data[PW_Start_Address+PW_n+3]-1) )
	  return BAD;
	PW_n += 3;
	continue;
      }
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xff) == 0x7f )
    {
      continue;
    }
    
    /* no Fx nor FxArg */
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0xf0 )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0x1F) > PW_k )
      {
/*printf ( "#9,1 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0x70 )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0x1F) > PW_k )
      {
/*printf ( "#9,2 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 1;
      continue;
    }
    /* no Note nor Sample number */
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0xe0 )
    {
      PW_n += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0x60 )
    {
      PW_n += 1;
      continue;
    }

    if ( (in_data[PW_Start_Address+PW_n]&0x80) == 0x80 )
    {
      if ( (((in_data[PW_Start_Address+PW_n]<<4)&0x10) | ((in_data[PW_Start_Address+PW_n+1]>>4)&0x0F)) > PW_k )
      {
/*printf ( "#9,3 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 3;
      continue;
    }

    if ( (((in_data[PW_Start_Address+PW_n]<<4)&0x10) | ((in_data[PW_Start_Address+PW_n+1]>>4)&0x0F)) > PW_k )
    {
/*printf ( "#9,4 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
      return BAD;
    }
    PW_n += 2;
  }

  /* PW_WholeSampleSize is the whole sample data size */
  /* PW_j is the address of the sample data */
  return GOOD;
}


/******************/
/* packed samples */
/******************/
short testP61A_pack ( void )
{
  if ( PW_i < 11 )
  {
    return BAD;
  }
  PW_Start_Address = PW_i-11;

  /* number of pattern (real) */
  PW_m = in_data[PW_Start_Address+2];
  if ( (PW_m > 0x7f) || (PW_m == 0) )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_m is the real number of pattern */

  /* number of sample */
  PW_k = in_data[PW_Start_Address+3];
  if ( (PW_k&0x40) != 0x40 )
  {
/*printf ( "#2,0 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_k &= 0x3F;
  if ( (PW_k > 0x1F) || (PW_k == 0) )
  {
/*printf ( "#2,1 Start:%ld (PW_k:%ld)\n" , PW_Start_Address,PW_k );*/
    return BAD;
  }
  /* PW_k is the number of sample */

  /* test volumes */
  if ( (PW_Start_Address+11+(PW_k*6))>PW_in_size)
    return BAD;
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+11+PW_l*6] > 0x40 )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test fines */
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( (in_data[PW_Start_Address+10+PW_l*6]&0x3F) > 0x0F )
    {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes and loop start */
  PW_WholeSampleSize = 0;
  for ( PW_n=0 ; PW_n<PW_k ; PW_n++ )
  {
    PW_o = ( (in_data[PW_Start_Address+8+PW_n*6]*256) +
	     in_data[PW_Start_Address+9+PW_n*6] );
    if ( ((PW_o < 0xFFDF) && (PW_o > 0x8000)) || (PW_o == 0) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_o < 0xFF00 )
      PW_WholeSampleSize += (PW_o*2);

    PW_j = ( (in_data[PW_Start_Address+12+PW_n*6]*256) +
	     in_data[PW_Start_Address+13+PW_n*6] );
    if ( (PW_j != 0xFFFF) && (PW_j >= PW_o) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_o > 0xFFDF )
    {
      if ( (0xFFFF-PW_o) > PW_k )
      {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
        return BAD;
      }
    }
  }

  /* test sample data address */
  PW_j = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1];
  if ( PW_j < (PW_k*6+8+PW_m*8) )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the address of the sample data */


  /* test track table */
  for ( PW_l=0 ; PW_l<(PW_m*4) ; PW_l++ )
  {
    PW_o = ((in_data[PW_Start_Address+8+PW_k*6+PW_l*2]*256)+
            in_data[PW_Start_Address+8+PW_k*6+PW_l*2+1] );
    if ( (PW_o+PW_k*6+8+PW_m*8) > PW_j )
    {
/*printf ( "#7 Start:%ld (value:%ld)(where:%x)(PW_l:%ld)(PW_m:%ld)(PW_o:%ld)\n"
, PW_Start_Address
, (in_data[PW_Start_Address+PW_k*6+8+PW_l*2]*256)+in_data[PW_Start_Address+8+PW_k*6+PW_l*2+1]
, PW_Start_Address+PW_k*6+8+PW_l*2
, PW_l
, PW_m
, PW_o );*/
      return BAD;
    }
  }

  /* test pattern table */
  PW_l=0;
  PW_o=0;
  /* first, test if we dont oversize the input file */
  if ( (PW_k*6+8+PW_m*8) > PW_in_size )
  {
/*printf ( "8,0 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  while ( (in_data[PW_Start_Address+PW_k*6+8+PW_m*8+PW_l] != 0xFF) && (PW_l<128) )
  {
    if ( in_data[PW_Start_Address+PW_k*6+8+PW_m*8+PW_l] > (PW_m-1) )
    {
/*printf ( "#8,1 Start:%ld (value:%ld)(where:%x)(PW_l:%ld)(PW_m:%ld)(PW_k:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_k*6+8+PW_m*8+PW_l]
, PW_Start_Address+PW_k*6+8+PW_m*8+PW_l
, PW_l
, PW_m
, PW_k );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_k*6+8+PW_m*8+PW_l] > PW_o )
      PW_o = in_data[PW_Start_Address+PW_k*6+8+PW_m*8+PW_l];
    PW_l++;
  }
  if ( (PW_l == 0) || (PW_l == 128) )
  {
/*printf ( "#8.2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_o += 1;
  /* PW_o is the highest number of pattern */


  /* test notes ... pfiew */
  PW_l += 1;
  for ( PW_n=(PW_k*6+8+PW_m*8+PW_l) ; PW_n<PW_j ; PW_n++ )
  {
    if ( (in_data[PW_Start_Address+PW_n]&0xff) == 0xff )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0x00 )
      {
	PW_n += 1;
	continue;
      }
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0x40 )
      {
	PW_n += 2;
	continue;
      }
      if ( (in_data[PW_Start_Address+PW_n+1]&0xc0) == 0xc0 )
      {
	PW_n += 3;
	continue;
      }
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xff) == 0x7f )
    {
      continue;
    }
    
    /* no Fx nor FxArg */
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0xf0 )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0x1F) > PW_k )
      {
/*printf ( "#9,1 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0x70 )
    {
      if ( (in_data[PW_Start_Address+PW_n+1]&0x1F) > PW_k )
      {
/*printf ( "#9,1 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 1;
      continue;
    }
    /* no Note nor Sample number */
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0xe0 )
    {
      PW_n += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n]&0xf0) == 0x60 )
    {
      PW_n += 1;
      continue;
    }

    if ( (in_data[PW_Start_Address+PW_n]&0x80) == 0x80 )
    {
      if ( (((in_data[PW_Start_Address+PW_n]<<4)&0x10) | ((in_data[PW_Start_Address+PW_n+1]>>4)&0x0F)) > PW_k )
      {
/*printf ( "#9,1 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
	return BAD;
      }
      PW_n += 3;
      continue;
    }

    if ( (((in_data[PW_Start_Address+PW_n]<<4)&0x10) | ((in_data[PW_Start_Address+PW_n+1]>>4)&0x0F)) > PW_k )
    {
/*printf ( "#9,1 Start:%ld (value:%ld) (where:%x) (PW_n:%ld) (PW_j:%ld)\n"
, PW_Start_Address
, in_data[PW_Start_Address+PW_n]
, PW_Start_Address+PW_n
, PW_n
, PW_j
 );*/
      return BAD;
    }
    PW_n += 2;
  }


  /* PW_WholeSampleSize is the whole sample data size */
  /* PW_j is the address of the sample data */
  return GOOD;
}

