/*
 *   ProRunner2.c   1996-1999 (c) Asle / ReDoX
 *
 * Converts ProRunner v2 packed MODs back to Protracker
 ********************************************************
 * 12 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 *
 * 28 Nov 1999 : Update
 *   - optimized code for size and speed (again :)
 *     Heh, 1/5 shorter now !.
 *
 * 23 aug 2001 : debug
 *   - "repeat last note used bug" pointed out by Markus Jaegermeister !.
 *     thanks !. 
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Depack_PRUN2 ( void )
{
  Uchar poss[37][2];
  Uchar Voices[4][4];
  Uchar *Whatever;
  long WholeSampleSize=0;
  long i=0,j=0;
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

  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever , 1024);
  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  Where += 8;

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );
    /* size */
    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  fwrite ( &in_data[Where] , 1 , 1 , out );
  Where += 1;

  /* noisetracker byte */
  fwrite ( &in_data[Where] , 1 , 1 , out );
  Where += 1;

  Whatever[256] = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    fwrite ( &in_data[Where] , 1 , 1 , out );
    Whatever[256] = ( in_data[Where] > Whatever[256] ) ? in_data[Where] : Whatever[256];
    Where += 1;
  }
  /*printf ( "Number of pattern : %d\n" , Whatever[256] );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data stuff */
  Where = PW_Start_Address + 770;
  for ( i=0 ; i<=Whatever[256] ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Whatever[0] = in_data[Where++];
      Whatever[100]=Whatever[101]=Whatever[102]=Whatever[103]=0x00;
      if ( Whatever[0] == 0x80 )
      {
	fwrite ( &Whatever[100] , 4 , 1 , out );
      }
      else if ( Whatever[0] == 0xC0 )
      {
	fwrite ( Voices[j%4] , 4 , 1 , out );
      }
      else if ( Whatever[0] != 0xC0 )
      {
        Whatever[1] = in_data[Where++];
        Whatever[2] = in_data[Where++];
	
	Whatever[100] = (Whatever[1]&0x80)>>3;
        Whatever[100] |= poss[(Whatever[0]>>1)][0];
        Whatever[101] = poss[(Whatever[0]>>1)][1];
	Whatever[102] = (Whatever[1]&0x70) << 1;
	Whatever[102] |= (Whatever[0]&0x01)<<4;
	Whatever[102] |= (Whatever[1]&0x0f);
	Whatever[103] = Whatever[2];
	
	fwrite ( &Whatever[100] , 4 , 1 , out );

        /* rol previous values */
        Voices[j%4][0] = Whatever[100];
        Voices[j%4][1] = Whatever[101];
        Voices[j%4][2] = Whatever[102];
        Voices[j%4][3] = Whatever[103];
      }
    }
  }
  free ( Whatever );


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "   ProRunner v2   " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[Prorunner2 to ]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[ProTracker]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

