#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTheDarkDemon ( void )
{
  /* test #1 */
  if ( PW_i < 137 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  PW_Start_Address = PW_i-137;

  /* len of file */
  if ( (PW_Start_Address + 564) >= PW_in_size )
  {
/*printf ( "#1,1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 (volumes,sample addresses and whole sample size) */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* sample address */
    PW_k = (in_data[PW_Start_Address+PW_j*14+130]*256*256*256)
          +(in_data[PW_Start_Address+PW_j*14+131]*256*256)
          +(in_data[PW_Start_Address+PW_j*14+132]*256)
          + in_data[PW_Start_Address+PW_j*14+133];
    /* sample size */
    PW_l = (((in_data[PW_Start_Address+PW_j*14+134]*256)+in_data[PW_Start_Address+PW_j*14+135])*2);
    /* loop start address */
    PW_m = (in_data[PW_Start_Address+PW_j*14+138]*256*256*256)
          +(in_data[PW_Start_Address+PW_j*14+139]*256*256)
          +(in_data[PW_Start_Address+PW_j*14+140]*256)
          + in_data[PW_Start_Address+PW_j*14+141];
    /* loop size (replen) */
    PW_n = (((in_data[PW_Start_Address+PW_j*14+142]*256)+in_data[PW_Start_Address+PW_j*14+143])*2);

    /* volume > 40h ? */
    if ( in_data[PW_Start_Address+PW_j*14+137] > 0x40 )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* loop start addy < sampl addy ? */
    if ( PW_m < PW_k )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* addy < 564 ? */
    if ( (PW_k < 564) || (PW_m < 564) )
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* loop start > size ? */
    if ( (PW_m-PW_k) > PW_l )
    {
/*printf ( "#2,3 (start:%ld)(SmpAddy:%ld)(loopAddy:%ld)(Size:%ld)\n"
         , PW_Start_Address , PW_k , PW_m , PW_l );*/
      return BAD;
    }

    /* loop start+replen > size ? */
    if ( ((PW_m-PW_k)+PW_n) > (PW_l+2) )
    {
/*printf ( "#2,31 (start:%ld)(size:%ld)(loopstart:%ld)(replen:%ld)\n"
         , PW_Start_Address , PW_l , PW_m-PW_k , PW_n );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_l;
  }

  if ( (PW_WholeSampleSize <= 2) || (PW_WholeSampleSize>(31*65535)) )
  {
/*printf ( "#2,4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


  /* test #3 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the whole sample size :) */
  if ( (PW_WholeSampleSize + 564) > PW_in_size )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test size of pattern list */
  if ( (in_data[PW_Start_Address] > 0x7f) || (in_data[PW_Start_Address]==0x00) )
  {
/*printf ( "#4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_j+2] > 0x7f )
    {
/*printf ( "#4,01 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_j+2] > PW_k )
      PW_k = in_data[PW_Start_Address+PW_j+2];
  }
  PW_k += 1;
  PW_k *= 1024;

  /* test end of pattern list */
  for ( PW_j=in_data[PW_Start_Address]+2 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_j+2] != 0 )
    {
/*printf ( "#4,02 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  

  /* test if not out of file range */
  if ( (PW_Start_Address + PW_WholeSampleSize+564+PW_k) > PW_in_size )
  {
/*printf ( "#4,1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


  /* PW_WholeSampleSize is the whole sample data size */
  /* PW_k is the whole pattern data size */
  /* test pattern data now ... */
  PW_l = PW_Start_Address+564+PW_WholeSampleSize;
  /* PW_l points on pattern data */
  for ( PW_j=0 ; PW_j<PW_k ; PW_j+=4 );
  {
    /* sample number > 31 ? */
    if ( in_data[PW_l+PW_j] > 0x1f )
    {
/*printf ( "#5,0 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* note > 0x48 (36*2) */
    if ( (in_data[PW_l+PW_j+1] > 0x48) || ((in_data[PW_l+PW_j+1]&0x01) == 0x01 ) )
    {
/*printf ( "#5,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=C and FxArg > 64 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0c)&&(in_data[PW_l+PW_j+3]>0x40) )
    {
/*printf ( "#5,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=D and FxArg > 64 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0d)&&(in_data[PW_l+PW_j+3]>0x40) )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=B and FxArg > 127 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0b)&&(in_data[PW_l+PW_j+3]>0x7f) )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }



  /* let's get free another var .. */
  PW_WholeSampleSize += PW_k;

  return GOOD;
}


