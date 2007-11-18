/* testUNIC_withID() */
/* testUNIC_withemptyID() */
/* testUNIC_noID() */
/* Rip_UNIC_withID() */
/* Rip_UNIC_noID() */
/* Depack_UNIC() */


/*
 * 27 dec 2001 : added some checks to prevent reading outside of input file.
 * 18 mar 2003 : a small bug in first test ... 
*/

#include "globals.h"
#include "extern.h"


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




void Rip_UNIC_withID ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1084;

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v1 module", UNIC_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}


void Rip_UNIC_noID ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1080;

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v1 module", UNIC_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 46);  /* 45 should do but call it "just to be sure" :) */
}



/*
 *   Unic_Tracker.c   1997 (c) Asle / ReDoX
 *
 * 
 * Unic tracked MODs to Protracker
 * both with or without ID Unic files will be converted
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 *
*/

#define ON 1
#define OFF 2

void Depack_UNIC ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar NumberOfPattern=0x00;
  Uchar poss[37][2];
  Uchar Max=0x00;
  Uchar Smp,Note,Fx,FxVal;
  Uchar fine=0x00;
  Uchar Pattern[1025];
  Uchar LOOP_START_STATUS=OFF;  /* standard /2 */
  long i=0,j=0,k=0,l=0;
  long WholeSampleSize=0;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  fwrite ( &in_data[Where] , 20 , 1 , out );
  Where += 20;


  for ( i=0 ; i<31 ; i++ )
  {
    /* sample name */
    fwrite ( &in_data[Where] , 20 , 1 , out );
    c1 = 0x00;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c1 , 1 , 1 , out );
    Where += 20;

    /* fine on ? */
    c1 = in_data[Where++];
    c2 = in_data[Where++];
    j = (c1*256)+c2;
    if ( j != 0 )
    {
      if ( j < 256 )
        fine = 0x10-c2;
      else
        fine = 0x100-c2;
    }
    else
      fine = 0x00;

    /* smp size */
    c1 = in_data[Where++];
    c2 = in_data[Where++];
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    l = ((c1*256)+c2)*2;
    WholeSampleSize += l;

    /* fine */
    Where += 1;
    fwrite ( &fine , 1 , 1 , out );

    /* vol */
    fwrite ( &in_data[Where++] , 1 , 1 , out );

    /* loop start */
    c1 = in_data[Where++];
    c2 = in_data[Where++];

    /* loop size */
    c3 = in_data[Where++];
    c4 = in_data[Where++];

    j=((c1*256)+c2)*2;
    k=((c3*256)+c4)*2;
    if ( (((j*2) + k) <= l) && (j!=0) )
    {
      LOOP_START_STATUS = ON;
      c1 *= 2;
      j = c2*2;
      if ( j>256 )
        c1 += 1;
      c2 *= 2;
    }

    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );

    fwrite ( &c3 , 1 , 1 , out );
    fwrite ( &c4 , 1 , 1 , out );
  }


/*  printf ( "whole sample size : %ld\n" , WholeSampleSize );*/
/*
  if ( LOOP_START_STATUS == ON )
    printf ( "!! Loop start value was /4 !\n" );
*/
  /* number of pattern */
  NumberOfPattern = in_data[Where++];
  fwrite ( &NumberOfPattern , 1 , 1 , out );

  /* noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );
  Where += 1;

  /* Pattern table */
  fwrite ( &in_data[Where] , 128 , 1 , out );
  Where += 128;

  /* get highest pattern number */
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[PW_Start_Address+952+i] > Max )
      Max = in_data[PW_Start_Address+952+i];
  }
  Max += 1;  /* coz first is $00 */

  c1 = 'M';
  c2 = '.';
  c3 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );

  /* verify UNIC ID */
  Where = PW_Start_Address + 1080;
  if ( (strncmp ( (char *)&in_data[Where] , "M.K." , 4 ) == 0) ||
       (strncmp ( (char *)&in_data[Where] , "UNIC" , 4 ) == 0) ||
       ((in_data[Where]==0x00)&&(in_data[Where+1]==0x00)&&(in_data[Where+2]==0x00)&&(in_data[Where+3]==0x00)))
    Where = PW_Start_Address + 1084l;
  else
    Where = PW_Start_Address + 1080l;


  /* pattern data */
  for ( i=0 ; i<Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Smp = ((in_data[Where+j*3]>>2) & 0x10) | ((in_data[Where+j*3+1]>>4)&0x0f);
      Note = in_data[Where+j*3]&0x3f;
      Fx = in_data[Where+j*3+1]&0x0f;
      FxVal = in_data[Where+j*3+2];

      if ( Fx == 0x0d )  /* pattern break */
      {
/*        printf ( "!! [%x] -> " , FxVal );*/
        c4 = FxVal%10;
        c3 = FxVal/10;
        FxVal = 16;
        FxVal *= c3;
        FxVal += c4;
/*        printf ( "[%x]\n" , FxVal );*/
      }

      Pattern[j*4]   = (Smp&0xf0);
      Pattern[j*4]  |= poss[Note][0];
      Pattern[j*4+1] = poss[Note][1];
      Pattern[j*4+2] = ((Smp<<4)&0xf0)|Fx;
      Pattern[j*4+3] = FxVal;
    }
    fwrite ( Pattern , 1024 , 1 , out );
    Where += 768;
  }


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  Crap ( "   UNIC Tracker   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

