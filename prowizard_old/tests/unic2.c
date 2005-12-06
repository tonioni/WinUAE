/* update on the 3rd of april 2000 */
/* bugs pointed out by Thomas Neumann .. thx :) */

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testUNIC2 ( void )
{
  /* test 1 */
  if ( (PW_i < 25) || ((PW_i+1828)>=PW_in_size) ) /* 1828=Head+1 pat */
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 ID = $00000000 ? */
  PW_Start_Address = PW_i-25;
  if (    (in_data[PW_Start_Address+1060] == 00)
	&&(in_data[PW_Start_Address+1061] == 00)
	&&(in_data[PW_Start_Address+1062] == 00)
	&&(in_data[PW_Start_Address+1063] == 00) )
  {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test 2,5 :) */
  PW_o=0;
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = ((in_data[PW_Start_Address+22+PW_k*30]*256)+in_data[PW_Start_Address+23+PW_k*30]);
    PW_m = ((in_data[PW_Start_Address+26+PW_k*30]*256)+in_data[PW_Start_Address+27+PW_k*30]);
    PW_n = ((in_data[PW_Start_Address+28+PW_k*30]*256)+in_data[PW_Start_Address+29+PW_k*30]);
    PW_WholeSampleSize += (PW_j*2);
    if ( test_smps ( PW_j, PW_m, PW_n, in_data[PW_Start_Address+25+PW_k*30], 0) == BAD )
    {
/*printf ( "#2,1 (Start:%ld) (PW_j:%ld) (PW_m:%ld) (PW_n:%ld) (PW_k:%ld)\n" , PW_Start_Address,PW_j,PW_m,PW_n,PW_k );*/
      return BAD;
    }
    if ( (PW_j>0x7fff) ||
         (PW_m>0x7fff) ||
         (PW_n>0x7fff) )
    {
/*printf ( "#2,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (((in_data[PW_Start_Address+20+PW_k*30]*256)+in_data[PW_Start_Address+21+PW_k*30]) != 0) && (PW_j == 0) )
    {
/*printf ( "#3,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+25+PW_k*30]!=0) && (PW_j == 0) )
    {
/*printf ( "#3,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* get the highest !0 sample */
    if ( PW_j != 0 )
      PW_o = PW_j+1;
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#3,3 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+930];
  if ( (PW_l>127) || (PW_l==0) )
  {
/*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+932+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+932+PW_j];
    if ( in_data[PW_Start_Address+932+PW_j] > 127 )
    {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* just to be sure .. */
  while ( PW_j != 128 )
  {
    if ( in_data[PW_Start_Address+932+PW_j] != 0 )
    {
/*printf ( "#4,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*768)+1060+PW_Start_Address+PW_WholeSampleSize) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* relative note number + last bit of sample > $34 ? */
    if ( in_data[PW_Start_Address+1060+PW_j*3] > 0x74 )
    {
/*printf ( "#5,1 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3 );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+1060+PW_j*3]&0x3F) > 0x24 )
    {
/*printf ( "#5,2 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1060+PW_j*3+1]&0x0F) == 0x0C) &&
         (in_data[PW_Start_Address+1060+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,3 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1060+PW_j*3+1]&0x0F) == 0x0B) &&
         (in_data[PW_Start_Address+1060+PW_j*3+2] > 0x7F) )
    {
/*printf ( "#5,4 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1060+PW_j*3+1]&0x0F) == 0x0D) &&
         (in_data[PW_Start_Address+1060+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,5 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3+2 );*/
      return BAD;
    }
    PW_n = ((in_data[PW_Start_Address+1060+PW_j*3]>>2)&0x30)|((in_data[PW_Start_Address+1061+PW_j*3+1]>>4)&0x0F);
    if ( PW_n > PW_o )
    {
/*printf ( "#5,6 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1060+PW_j*3 );*/
      return BAD;
    }
  }

  return GOOD;
}

