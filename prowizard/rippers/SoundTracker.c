/* testSoundTracker() */
/* Rip_SoundTracker() */


#include "globals.h"
#include "extern.h"

short testSoundTracker ( void )
{
  /* test 1 */
  /* start of stk before start of file ? */
  if ( (PW_i < 45) || ((PW_i+555)>PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  /* samples tests */
  PW_Start_Address = PW_i-45;
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);
    /* all sample sizes */
    PW_WholeSampleSize += PW_j;

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
/*printf ( "#2,1\n" );*/
      return BAD;
    }
    /* size & loopstart !=0 & size=loopstart ? */
    if ( (PW_j != 0) && (PW_j==PW_m) )
    {
/*printf ( "#2,15\n" );*/
      return BAD;
    }
    /* size =0 & loop start !=0 */
    if ( (PW_j==0) && (PW_m!=0) )
    {
/*printf ( "#2,2\n" );*/
      return BAD;
    }
  }
  /* all sample sizes < 8 ? */
  if ( PW_WholeSampleSize<8 )
  {
/*printf ( "#2,3\n" );*/
    return BAD;
  }

  /* test #3  finetunes & volumes */
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+44+PW_k*30]>0x0f) || (in_data[PW_Start_Address+45+PW_k*30]>0x40) )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+470];
  if ( (PW_l>127) || (PW_l==0) )
  {
/*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+472+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+472+PW_j];
    if ( in_data[PW_Start_Address+472+PW_j] > 127 )
    {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* found some obscure stk :( */
  while ( PW_j != 128 )
  {
    if ( in_data[PW_Start_Address+472+PW_j] != 0 )
    {
/*printf ( "#4,2 (Start:%ld) (PW_j:%ld) (at:%ld)\n" , PW_Start_Address,PW_j ,PW_Start_Address+472+PW_j );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+600+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* sample > 1f   or   pitch > 358 ? */
    if ( in_data[PW_Start_Address+600+PW_j*4] > 0x13 )
    {
/*printf ( "#5.1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_l = ((in_data[PW_Start_Address+600+PW_j*4]&0x0f)*256)+in_data[PW_Start_Address+601+PW_j*4];
    if ( (PW_l>0) && (PW_l<0x71) )
    {
/*printf ( "#5,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;                                         
}




void Rip_SoundTracker ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is still the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 600;

  CONVERT = BAD;
  Save_Rip ( "SoundTracker module", SoundTracker );
  
  if ( Save_Status == GOOD )
    PW_i += 46;  /* after 1st volume */
}

