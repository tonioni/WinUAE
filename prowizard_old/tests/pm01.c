/*
- 22 sept 2001 -
bugfix : test #5 was fake since first pat addy can be != $00000000
*/


#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


short testPM01 ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_Start_Address + 766)>PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-3;
  PW_l=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);
    PW_l += PW_k;
    /* finetune > 0x0f ? */
    if ( in_data[PW_Start_Address+2+8*PW_j] > 0x0f )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2) > PW_k )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( PW_l <= 2 )
  {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3   about size of pattern list */
  PW_l = (in_data[PW_Start_Address+248]*256)+in_data[PW_Start_Address+249];
  PW_k = PW_l/4;
  if ( (PW_k*4) != PW_l )
  {
/*printf ( "#3 (start:%ld)(PW_l:%ld)(PW_k:%ld)\n" , PW_Start_Address,PW_l,PW_k );*/
    return BAD;
  }
  if ( PW_k>127 )
  {
/*printf ( "#3,1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_l == 0 )
  {
/*printf ( "#3,2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #4  size of all the pattern data */
  /* PW_k contains the size of the pattern list */
  PW_l = (in_data[PW_Start_Address+762]*256*256*256)
    +(in_data[PW_Start_Address+743]*256*256)
    +(in_data[PW_Start_Address+764]*256)
    +in_data[PW_Start_Address+765];
  if ( (PW_l<1024) || (PW_l>131072) || ((PW_l+PW_Start_Address)>PW_in_size) )
  {
/*printf ( "#4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #5  first pattern address != $00000000 ? */
  /* bugfix : removed coz first addy can be != $00000000 ! */

  /* test #6  pattern addresses */
  /* PW_k is still ths size of the pattern list */
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_l = (in_data[PW_Start_Address+250+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+251+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+252+PW_j*4]*256)
      +in_data[PW_Start_Address+253+PW_j*4];
    if ( PW_l > 131072 )
    {
/*printf ( "#6 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( ((PW_l/1024)*1024) != PW_l )
    {
      return BAD;
    }
  }

  /* test #7  last patterns in pattern table != $00000000 ? */
  PW_j += 4;  /* just to be sure */
  while ( PW_j != 128 )
  {
    PW_l = (in_data[PW_Start_Address+250+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+251+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+252+PW_j*4]*256)
      +in_data[PW_Start_Address+253+PW_j*4];
    if ( PW_l != 0 )
    {
/*printf ( "#7 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 1;
  }

  /* test #8 : first pattern data */
  for ( PW_j=0 ; PW_j<256 ; PW_j+=4 )
  {
    if ( (255-in_data[PW_Start_Address+766+PW_j])>0x13 )
    {
/*printf ( "#8 (Start:%ld)\n", PW_Start_Address);*/
      return BAD;
    }
  }

  return GOOD;
}


