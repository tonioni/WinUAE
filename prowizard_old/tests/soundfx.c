#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSoundFX13 ( void )
{
  /* test 1 */
  if ( PW_i < 0x3C )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  /* samples tests */
  PW_Start_Address = PW_i-0x3C;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    /* size */
    PW_j = ((in_data[PW_Start_Address+PW_k*4+2]*256)+in_data[PW_Start_Address+PW_k*4+3]);
    /* loop start */
    PW_m = ((in_data[PW_Start_Address+106+PW_k*30]*256)+in_data[PW_Start_Address+107+PW_k*30]);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+108+PW_k*30]*256)+in_data[PW_Start_Address+109+PW_k*30])*2);
    /* all sample sizes */

    /* size,loopstart,replen > 64k ? */
    if ( (PW_j > 0xFFFF) || (PW_m > 0xFFFF) || (PW_n > 0xFFFF) )
    {
/*printf ( "#2,0 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* replen > size ? */
    if ( PW_n > (PW_j+2) )
    {
/*printf ( "#2 (Start:%ld) (smp:%ld) (size:%ld) (replen:%ld)\n"
         , PW_Start_Address , PW_k+1 , PW_j , PW_n );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( PW_m > PW_j )
    {
/*printf ( "#2,0 (Start:%ld) (smp:%ld) (size:%ld) (lstart:%ld)\n"
         , PW_Start_Address , PW_k+1 , PW_j , PW_m );*/
      return BAD;
    }
    /* loop size =0 & loop start != 0 ?*/
    if ( (PW_m != 0) && (PW_n==0) )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* size & loopstart !=0 & size=loopstart ? */
    if ( (PW_j != 0) && (PW_j==PW_m) )
    {
/*printf ( "#2,15 (start:%ld) (smp:%ld) (siz:%ld) (lstart:%ld)\n"
         , PW_Start_Address,PW_k+1,PW_j,PW_m );*/
      return BAD;
    }
    /* size =0 & loop start !=0 */
    if ( (PW_j==0) && (PW_m!=0) )
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* get real whole sample size */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<15 ; PW_j++ )
  {
    PW_k = ((in_data[PW_Start_Address+PW_j*4]*256*256*256)+
            (in_data[PW_Start_Address+PW_j*4+1]*256*256)+
            (in_data[PW_Start_Address+PW_j*4+2]*256)+
             in_data[PW_Start_Address+PW_j*4+3] );
    if ( PW_k > 131072 )
    {
/*printf ( "#2,4 (start:%ld) (smp:%ld) (size:%ld)\n"
         , PW_Start_Address,PW_j,PW_k );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_k;
  }

  /* test #3  finetunes & volumes */
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+105+PW_k*30]>0x40 )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+0x212];
  if ( (PW_l>127) || (PW_l==0) )
  {
/*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+0x214+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+0x214+PW_j];
    if ( in_data[PW_Start_Address+0x214+PW_j] > 127 )
    {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+0x294+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;                                         
}

