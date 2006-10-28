/* testPRUN2() */
/* Rip_PRUN2() */
/* Depack_PRUN2() */


#include "globals.h"
#include "extern.h"

short testPRUN2 ( void )
{
  PW_Start_Address = PW_i;
  PW_j = (in_data[PW_i+4]*256*256*256)+(in_data[PW_i+5]*256*256)+(in_data[PW_i+6]*256)+in_data[PW_i+7];

  /* test sample_data address */
  if ( (PW_j+PW_Start_Address) > PW_in_size )
  {
    return BAD;
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+11+PW_k*8] > 0x40 )
    {
      return BAD;
    }
  }

  /* test finetunes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+10+PW_k*8] > 0x0F )
    {
      return BAD;
    }
  }

  return GOOD;
}



void Rip_PRUN2 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+8+PW_k*8]*256)+in_data[PW_Start_Address+9+PW_k*8])*2);
  
  OutputSize = PW_j + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "Prorunner 2 module", ProRunner_v2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* -1 should do but call it "just to be sure" :) */
}




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

void Depack_PRUN2 ( void )
{
  Uchar poss[37][2];
  Uchar Voices[4][4];
  Uchar *Whatever;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

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

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

