#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testGPMO ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    /*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps (PW_j,PW_m,PW_n,in_data[PW_Start_Address+45+PW_k*30]/2,in_data[PW_Start_Address+44+PW_k*30]) == BAD )
    {
      /*      printf ( "#2 (Start:%ld)(siz:%ld)(lstart:%ld)(lsiz:%ld)\n" , PW_Start_Address,PW_j,PW_m,PW_n );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+950];
  if ( (PW_l>127) || (PW_l==0) )
  {
    /*    printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+952+PW_j];
    if ( in_data[PW_Start_Address+952+PW_j] > 127 )
    {
      /*      printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* found some obscure ptk :( */
  while ( PW_j < 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > 0x7f )
    {
      /*      printf ( "#4,2 (Start:%ld) (PW_j:%ld) (at:%ld)\n" , PW_Start_Address,PW_j ,PW_Start_Address+952+PW_j );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;

  /*  if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+1084+PW_Start_Address) > PW_in_size )
  {
    /*    printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* sample > 1f   or   pitch > 358 ? */
    if ( in_data[PW_Start_Address+1084+PW_j*4] > 0x13 )
    {
      /*      printf ( "#5.1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_l = ((in_data[PW_Start_Address+1084+PW_j*4]&0x0f)*256)+in_data[PW_Start_Address+1085+PW_j*4];
    if ( (PW_l>0) && (PW_l<0x1C) )
    {
      /*      printf ( "#5,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}

