/*  (May 2002)
 *    added test_smps()
*/
/* testMP_noID() */
/* testMP_withID() */
/* Rip_MP_noID() */
/* Rip_MP_withID() */
/* Depack_MP() */

#include "globals.h"
#include "extern.h"


short testMP_noID ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_i+375)>PW_in_size))
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-3;
  PW_l=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+8*PW_j]*256)+in_data[PW_Start_Address+1+8*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+4+8*PW_j]*256)+in_data[PW_Start_Address+5+8*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+6+8*PW_j]*256)+in_data[PW_Start_Address+7+8*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+3+8*PW_j], in_data[PW_Start_Address+2+8*PW_j] ) == BAD )
    {
      /*      printf ( "#2 Start:%ld (siz:%ld)(lstart:%ld)(lsiz:%ld)\n", PW_Start_Address,PW_k,PW_m,PW_n );*/
      return BAD; 
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
    /*printf(  "#2,5 (start:%ld)\n",PW_Start_Address );*/
    return BAD;
  }

  /* test #3 */
  PW_l = in_data[PW_Start_Address+248];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#3 (Start:%ld)\n",PW_Start_Address );*/
    return BAD;
  }

  /* test #4 */
  /* PW_l contains the size of the pattern list */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+250+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+250+PW_j];
    if ( in_data[PW_Start_Address+250+PW_j] > 0x7f )
    {
      /*printf(  "#4 (Start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
    if ( PW_j > PW_l+3 )
      if (in_data[PW_Start_Address+250+PW_j] != 0x00)
      {
	/*printf(  "#4,1 (Start:%ld)\n",PW_Start_Address );*/
        return BAD;
      }
  }
  PW_k += 1;

  /* test #5  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  /* PW_WholeSampleSize is the whole sample size */
  PW_m = 0;
  if ( PW_Start_Address + 379 + ((PW_k*256)*4) > PW_in_size )
  {
    return BAD;
  }
  for ( PW_j=0 ; PW_j<((256*PW_k)-4) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+378+PW_j*4+4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      /*printf(  "#5 (Start:%ld)(byte0:%x)(Where:%ld)\n",PW_Start_Address,in_data[PW_Start_Address+378+PW_j*4],PW_Start_Address+378+PW_j*4 );*/
      return BAD;
    }
    PW_l  = in_data[PW_Start_Address+378+PW_j*4]&0x0f;
    PW_l *= 256;
    PW_l += in_data[PW_Start_Address+379+PW_j*4];
    PW_n = in_data[PW_Start_Address+380+PW_j*4]>>4;
    if ( PW_l != 0 )
      PW_m = 1;
    if ( PW_n != 0 )
      PW_o = 1;
    if ( (PW_l > 0) && (PW_l<0x71) )
    {
      /*printf ( "#5,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+378+PW_j*4, PW_l );*/
      return BAD;
    }
  }
  if ( (PW_m == 0) || (PW_o == 0) )
  {
    /* no note ... odd */
    /*printf ("#5,2 (Start:%ld)\n",PW_Start_Address);*/
    return BAD;
  }

  /* test #6  (loopStart+LoopSize > Sample ? ) */
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+1+PW_j*8])*2);
    PW_l = (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2)
          +(((in_data[PW_Start_Address+6+PW_j*8]*256)+in_data[PW_Start_Address+7+PW_j*8])*2);
    if ( PW_l > (PW_k+2) )
    {
      /*printf(  "#6 (Start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}


short testMP_withID ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+6+8*PW_j] > 0x0f )
    {
      return BAD;
    }
  }

  /* test #2 */
  PW_l = in_data[PW_Start_Address+252];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    return BAD;
  }

  /* test #4 */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+254+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+254+PW_j];
    if ( in_data[PW_Start_Address+254+PW_j] > 0x7f )
    {
      return BAD;
    }
  }
  PW_k += 1;

  /* test #5  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  for ( PW_j=0 ; PW_j<(256*PW_k) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+382+PW_j*4];
    if ( PW_l > 19 )  /* 0x13 */
    {
      return BAD;
    }
  }

  return GOOD;
}



void Rip_MP_noID ( void )
{
  /*PW_WholeSampleSize is the whole sample size*/

  PW_j = in_data[PW_Start_Address+248];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
    if ( in_data[PW_Start_Address+250+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+250+PW_k];

  PW_k = (in_data[PW_Start_Address+378]*256*256*256)+
    (in_data[PW_Start_Address+379]*256*256)+
    (in_data[PW_Start_Address+380]*256)+
    in_data[PW_Start_Address+381];

  PW_l += 1;
  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 378;
  if ( PW_k == 0 )
    OutputSize += 4;

  CONVERT = GOOD;
  Save_Rip ( "Module Protector Packed music", Module_protector );
  
  if ( Save_Status == GOOD )
    PW_i += 0x57E;
  /*PW_i += (OutputSize - 5);  -- 4 should do but call it "just to be sure" :) */
}


void Rip_MP_withID ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2);
  PW_j = in_data[PW_Start_Address+252];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
    if ( in_data[PW_Start_Address+254+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+254+PW_k];

  PW_k = (in_data[PW_Start_Address+382]*256*256*256)+
    (in_data[PW_Start_Address+383]*256*256)+
    (in_data[PW_Start_Address+384]*256)+
    in_data[PW_Start_Address+385];

  PW_l += 1;
  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 382;

  /* not sure for the following test because I've never found */
  /* any MP file with "TRK1" ID. I'm basing all this on Gryzor's */
  /* statement in his Hex-dump exemple ... */
  if ( PW_k == 0 )
    OutputSize += 4;

  CONVERT = GOOD;
  Save_Rip ( "Module Protector Packed music", Module_protector );
  
  if ( Save_Status == GOOD )
    PW_i += 3;  /* 4 should do but call it "just to be sure" :) */
}


/*
 *   Module_Protector.c   1997 (c) Asle / ReDoX
 *
 * Converts MP packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 *  NOTE : It takes care of both MP packed files with or without ID !
 *
 * Last update: 28/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

void Depack_MP ( void )
{
  Uchar *Whatever;
  long i=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (Uchar *) malloc (64);
  BZERO ( Whatever , 64 );

  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  if ( (in_data[Where] == 'T') && (in_data[Where+1] == 'R') && (in_data[Where+2] == 'K') && (in_data[Where+3] == '1') )
    Where += 4;

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* pattern table lenght & Ntk byte */
  fwrite ( &in_data[Where] , 2 , 1 , out );
  Where += 2;

  Whatever[32] = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i] > Whatever[32] )
      Whatever[32] = in_data[Where+i];
  }
  fwrite ( &in_data[Where] , 128 , 1 , out );
  Where += 128;
  /*printf ( "Number of pattern : %d\n" , Max+1 );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* bypass 4 unknown empty bytes */
  if ( (in_data[Where]==0x00)&&(in_data[Where+1]==0x00)&&(in_data[Where+2]==0x00)&&(in_data[Where+3]==0x00) )
    Where += 4;
  /*else*/
    /*printf ( "! four empty bytes bypassed at the beginning of the pattern data\n" );*/

  /* pattern data */
  i = (Whatever[32]+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " Module Protector " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
