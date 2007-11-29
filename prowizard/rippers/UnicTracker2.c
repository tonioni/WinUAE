/* testUNIC2() */
/* Rip_UNIC2() */
/* Depack_UNIC2() */


/* update on the 3rd of april 2000 */
/* bugs pointed out by Thomas Neumann .. thx :) */

#include "globals.h"
#include "extern.h"


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



void Rip_UNIC2 ( void )
{
  /* PW_k is still the nbr of pattern */

  OutputSize = PW_WholeSampleSize + (PW_k*768) + 1060;

  CONVERT = GOOD;
  Save_Rip ( "UNIC tracker v2 module", UNIC_v2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 26);  /* 25 should do but call it "just to be sure" :) */
}


/*
 *   Unic_Tracker_2.c   1997 (c) Asle / ReDoX
 *
 * 
 * Unic tracked 2 MODs to Protracker
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 nov 1999 :
 *   - Overall Speed and Size optimizings.
*/

void Depack_UNIC2 ( void )
{
  Uchar poss[37][2];
  Uchar Smp,Note,Fx,FxVal;
  Uchar *Whatever;
/*  Uchar LOOP_START_STATUS=OFF;*/  /* standard /2 */
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
  Whatever = (Uchar *) malloc (1028);
  BZERO ( Whatever , 1028 );
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    /* sample name */
    fwrite ( &in_data[Where] , 20 , 1 , out );
    fwrite ( &Whatever[32] , 2 , 1 , out );

    /* fine on ? */
    j = (in_data[Where+20]*256)+in_data[Where+21];
    if ( j != 0 )
    {
      if ( j < 256 )
        Whatever[48] = 0x10-in_data[Where+21];
      else
        Whatever[48] = 0x100-in_data[Where+21];
    }

    /* smp size */
    fwrite ( &in_data[Where+22] , 2 , 1 , out );
    l = ((in_data[Where+22]*256)+in_data[Where+23])*2;
    WholeSampleSize += l;

    /* fine */
    fwrite ( &Whatever[48] , 1 , 1 , out );

    /* vol */
    fwrite ( &in_data[Where+25] , 1 , 1 , out );

    /* loop start */
    Whatever[64] = in_data[Where+26];
    Whatever[65] = in_data[Where+27];

    /* loop size */
    j=((Whatever[64]*256)+Whatever[65])*2;
    k=((in_data[Where+28]*256)+in_data[Where+29])*2;
    if ( (((j*2) + k) <= l) && (j!=0) )
    {
/*      LOOP_START_STATUS = ON;*/
      Whatever[64] *= 2;
      j = Whatever[65]*2;
      if ( j>256 )
        Whatever[64] += 1;
      Whatever[65] *= 2;
    }

    fwrite ( &Whatever[64] , 2 , 1 , out );

    fwrite ( &in_data[Where+28] , 2 , 1 , out );

    Where += 30;
  }


  /*printf ( "whole sample size : %ld\n" , WholeSampleSize );*/
/*
  if ( LOOP_START_STATUS == ON )
    printf ( "!! Loop start value was /4 !\n" );
*/
  /* number of pattern */
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* noisetracker byte */
  Whatever[32] = 0x7f;
  fwrite ( &Whatever[32] , 1 , 1 , out );
  Where += 1;

  /* Pattern table */
  fwrite ( &in_data[Where] , 128 , 1 , out );
  Where += 128;

  /* get highest pattern number */
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[PW_Start_Address+932+i] > Whatever[1026] )
      Whatever[1026] = in_data[PW_Start_Address+932+i];
  }
  /*printf ( "Number of Pattern : %d\n" , Whatever[1026]+1 );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* pattern data */
  for ( i=0 ; i<=Whatever[1026] ; i++ )
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
        Whatever[1024] = FxVal%10;
        Whatever[1025] = FxVal/10;
        FxVal = 16;
        FxVal *= Whatever[1025];
        FxVal += Whatever[1024];
/*        printf ( "[%x]\n" , FxVal );*/
      }

      Whatever[j*4]   = (Smp&0xf0);
      Whatever[j*4]  |= poss[Note][0];
      Whatever[j*4+1] = poss[Note][1];
      Whatever[j*4+2] = ((Smp<<4)&0xf0)|Fx;
      Whatever[j*4+3] = FxVal;
    }
    fwrite ( Whatever , 1024 , 1 , out );
    Where += 768;
  }
  free ( Whatever );


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  Crap ( "  UNIC Tracker 2  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

