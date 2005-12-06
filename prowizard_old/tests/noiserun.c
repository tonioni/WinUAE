#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testNoiserunner ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+6+PW_k*16]*256)+in_data[PW_Start_Address+7+PW_k*16])*2);
    if ( PW_j > 0xFFFF )
    {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* volumes */
    if ( in_data[PW_Start_Address+1+PW_k*16] > 0x40 )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if ( PW_WholeSampleSize == 0 )
  {
    return BAD;
  }
  /* PW_WholeSampleSize is the size of all the sample data */

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+950];
  if ( (PW_l>127) || (PW_l==0) )
  {
/*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+952+PW_j];
    if ( in_data[PW_Start_Address+952+PW_j] > 127 )
    {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  while ( PW_j != 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_j] != 0 )
    {
/*printf ( "#4,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;

  /* test if we read outside of the file */
  if ( (PW_Start_Address+PW_k*256) > PW_in_size )
    return BAD;

  /* test #5 pattern data ... */
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* note > 48h ? */
    if ( in_data[PW_Start_Address+1086+PW_j*4] > 0x48 )
    {
/*printf ( "#5.1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_l = in_data[PW_Start_Address+1087+PW_j*4];
    if ( ((PW_l/8)*8) != PW_l )
    {
/*printf ( "#5,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_l = in_data[PW_Start_Address+1084+PW_j*4];
    if ( ((PW_l/4)*4) != PW_l )
    {
/*printf ( "#5,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}


