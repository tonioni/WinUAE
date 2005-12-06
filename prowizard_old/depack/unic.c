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

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

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
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , "  -[UNIC Tracker]-  " );
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

