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

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

/*#define ON 1
#define OFF 2*/

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

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

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
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[UNIC Tracker 2]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "  -[2 ProTracker]-  " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

