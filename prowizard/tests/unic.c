/*
 * 27 dec 2001 : added some checks to prevent reading outside of input file.
 * 18 mar 2003 : a small bug in first test ... 
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testUNIC_withID ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    PW_WholeSampleSize += PW_j;
    PW_n = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2)
                        +(((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);
    if ( (PW_j+2) < PW_n )
    {
      /*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
    /*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3  finetunes & volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+44+PW_k*30]>0x0f) || (in_data[PW_Start_Address+45+PW_k*30]>0x40) )
    {
      /*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

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


  /* test #5 pattern data ... */
  if ( ((PW_k*768)+1084+PW_Start_Address) > PW_in_size )
  {
    /*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* relative note number + last bit of sample > $34 ? */
    if ( in_data[PW_Start_Address+1084+PW_j*3] > 0x74 )
    {
      /*printf ( "#5,1 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3 );*/
      return BAD;
    }
  }

  return GOOD;
}


short testUNIC_withemptyID ( void )
{
  /* test 1 */
  if ( (PW_i < 45) || ((PW_i-45+1084)>=PW_in_size) )
  {
    /*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 ID = $00000000 ? */
  PW_Start_Address = PW_i-45;
  if (    (in_data[PW_Start_Address+1080] != 00)
	&&(in_data[PW_Start_Address+1081] != 00)
	&&(in_data[PW_Start_Address+1082] != 00)
	&&(in_data[PW_Start_Address+1083] != 00) )
  {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test 2,5 :) */
  PW_WholeSampleSize = 0;
  PW_o = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);
    PW_WholeSampleSize += PW_j;

    if ( (PW_n != 0) && ((PW_j+2) < (PW_m+PW_n)) )
    {
/*printf ( "#2 (Start:%ld) (size:%ld)(lstart:%ld)(replen:%ld)\n" , PW_Start_Address,PW_j,PW_m,PW_n );*/
      return BAD;
    }
    if ( (PW_j>0xffff) ||
         (PW_m>0xffff) ||
         (PW_n>0xffff) )
    {
/*printf ( "#2,2 (Start:%ld) (at:%x)\n" , PW_Start_Address,PW_Start_Address+42+PW_k*30 );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+45+PW_k*30]>0x40 )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* finetune ... */
    if ( ( (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) != 0) && (PW_j == 0) ) ||
         ( (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) > 8 ) &&
           (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) < 247) ) )
    {
/*printf ( "#3,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start but no replen ? */
    if ( (PW_m!=0) && (PW_n<=2) )
    {
/*printf ( "#3,15 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+45+PW_k*30]!=0) && (PW_j == 0) )
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
/*printf ( "#2,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


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


  /* test #5 pattern data ... */
  if ( ((PW_k*768)+1084+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* relative note number + last bit of sample > $34 ? */
    if ( in_data[PW_Start_Address+1084+PW_j*3] > 0x74 )
    {
/*printf ( "#5,1 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3 );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+1084+PW_j*3]&0x3F) > 0x24 )
    {
/*printf ( "#5,2 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1084+PW_j*3+1]&0x0F) == 0x0C) &&
         (in_data[PW_Start_Address+1084+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,3 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1084+PW_j*3+1]&0x0F) == 0x0B) &&
         (in_data[PW_Start_Address+1084+PW_j*3+2] > 0x7F) )
    {
/*printf ( "#5,4 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1084+PW_j*3+1]&0x0F) == 0x0D) &&
         (in_data[PW_Start_Address+1084+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,5 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3+2 );*/
      return BAD;
    }
    PW_n = ((in_data[PW_Start_Address+1084+PW_j*3]>>2)&0x30)|((in_data[PW_Start_Address+1085+PW_j*3+1]>>4)&0x0F);
    if ( PW_n > PW_o )
    {
/*printf ( "#5,3 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1084+PW_j*3 );*/
      return BAD;
    }
  }

  return GOOD;
}



short testUNIC_noID ( void )
{
  /* test 1 */
  if ( (PW_i < 45) || ((PW_i-45+1083)>=PW_in_size) )
  {
    /*    printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 ID = $00000000 ? */
  PW_Start_Address = PW_i-45;
  if (    (in_data[PW_Start_Address+1080] == 00)
	&&(in_data[PW_Start_Address+1081] == 00)
	&&(in_data[PW_Start_Address+1082] == 00)
	&&(in_data[PW_Start_Address+1083] == 00) )
  {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test 2,5 :) */
  PW_WholeSampleSize=0;
  PW_o = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);
    PW_WholeSampleSize += PW_j;
    if ( (PW_n!=0) && ((PW_j+2) < (PW_m+PW_n)) )
    {
/*printf ( "#2,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* samples too big ? */
    if ( (PW_j>0xffff) ||
         (PW_m>0xffff) ||
         (PW_n>0xffff) )
    {
/*printf ( "#2,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* volume too big */
    if ( in_data[PW_Start_Address+45+PW_k*30]>0x40 )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* finetune ... */
    if ( ( (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) != 0) && (PW_j == 0) ) ||
         ( (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) > 8 ) &&
           (((in_data[PW_Start_Address+40+PW_k*30]*256)+in_data[PW_Start_Address+41+PW_k*30]) < 247) ) )
    {
/*printf ( "#3,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start but no replen ? */
    if ( (PW_m!=0) && (PW_n<=2) )
    {
/*printf ( "#3,15 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+45+PW_k*30]!=0) && (PW_j == 0) )
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


  /* test #5 pattern data ... */
  /* PW_o is the highest !0 sample */
  if ( ((PW_k*768)+1080+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* relative note number + last bit of sample > $34 ? */
    if ( in_data[PW_Start_Address+1080+PW_j*3] > 0x74 )
    {
/*printf ( "#5,1 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3 );*/
      return BAD;
    }
    if ( (in_data[PW_Start_Address+1080+PW_j*3]&0x3F) > 0x24 )
    {
/*printf ( "#5,2 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1080+PW_j*3+1]&0x0F) == 0x0C) &&
         (in_data[PW_Start_Address+1080+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,3 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1080+PW_j*3+1]&0x0F) == 0x0B) &&
         (in_data[PW_Start_Address+1080+PW_j*3+2] > 0x7F) )
    {
/*printf ( "#5,4 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3+1 );*/
      return BAD;
    }
    if ( ((in_data[PW_Start_Address+1080+PW_j*3+1]&0x0F) == 0x0D) &&
         (in_data[PW_Start_Address+1080+PW_j*3+2] > 0x40) )
    {
/*printf ( "#5,5 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3+2 );*/ 
      return BAD;
    }
    PW_n = ((in_data[PW_Start_Address+1080+PW_j*3]>>2)&0x30)|((in_data[PW_Start_Address+1081+PW_j*3+1]>>4)&0x0F);
    if ( PW_n > PW_o )
    {
/*printf ( "#5,9 (Start:%ld) (where:%ld)\n" , PW_Start_Address,PW_Start_Address+1080+PW_j*3 );*/
      return BAD;
    }
  }

  /* test #6  title coherent ? */
  for ( PW_j=0 ; PW_j<20 ; PW_j++ )
  {
    if ( ((in_data[PW_Start_Address+PW_j] != 0)
       &&(in_data[PW_Start_Address+PW_j] < 32))
       ||(in_data[PW_Start_Address+PW_j] > 180) )
    {
/*printf ( "#6 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
  /* PW_k is still the number of pattern */
}

