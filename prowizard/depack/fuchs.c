/*
 *   FuchsTracker.c   1999 (c) Sylvain "Asle" Chipaux
 *
 * Depacks Fucks Tracker modules
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 *   - small bug correction with loops (thx to Thomas Neumann
 *     for pointing this out !)
 * Another update : 23 nov 2003
 *   - used htonl() so that use of addy is now portable on 68k archs
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_FuchsTracker ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00;
  long WholeSampleSize=0;
  long SampleSizes[16];
  long LoopStart[16];
  unsigned long i=0,j=0,k;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( SampleSizes , 16*4 );
  BZERO ( LoopStart , 16*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* write empty ptk header */
  Whatever = (Uchar *) malloc ( 1080 );
  BZERO ( Whatever , 1080 );
  fwrite ( Whatever , 1080 , 1 , out );

  /* write title */
  fseek ( out , 0 , 0 );
  fwrite ( &in_data[Where] , 10 , 1 , out );
  Where += 10;

  /* read all sample data size */
  WholeSampleSize = ((in_data[Where]*256*256*256)+
                     (in_data[Where+1]*256*256)+
                     (in_data[Where+2]*256)+
                      in_data[Where+3] );
  Where += 4;
/*  printf ( "Whole Sample Size : %ld\n" , WholeSampleSize );*/


  /* read/write sample sizes */
  /* have to halve these :( */
  for ( i=0 ; i<16 ; i++ )
  {
    fseek ( out , 42+i*30 , 0 );
    Whatever[0] = in_data[Where];
    Whatever[1] = in_data[Where+1];
    SampleSizes[i] = (Whatever[0]*256)+Whatever[1];
    Whatever[1] /= 2;
    if ( (Whatever[0]/2)*2 != Whatever[0] )
    {
      if ( Whatever[1] < 0x80 )
        Whatever[1] += 0x80;
      else
      {
        Whatever[1] -= 0x80;
        Whatever[0] += 0x01;
      }
    }
    Whatever[0] /= 2;
    fwrite ( Whatever , 2 , 1 , out );
    Where += 2;
  }

  /* read/write volumes */
  for ( i=0 ; i<16 ; i++ )
  {
    fseek ( out , 45+i*30 , 0 );
    Where += 1;
    fwrite ( &in_data[Where++] , 1 , 1 , out );
  }

  /* read/write loop start */
  /* have to halve these :( */
  for ( i=0 ; i<16 ; i++ )
  {
    fseek ( out , 46+i*30 , 0 );
    Whatever[0] = in_data[Where];
    Whatever[1] = in_data[Where+1];
    LoopStart[i] = (Whatever[0]*256)+Whatever[1];
    Whatever[1] /= 2;
    if ( (Whatever[0]/2)*2 != Whatever[0] )
    {
      if ( Whatever[1] < 0x80 )
        Whatever[1] += 0x80;
      else
      {
        Whatever[1] -= 0x80;
        Whatever[0] += 0x01;
      }
    }
    Whatever[0] /= 2;
    fwrite ( Whatever , 2 , 1 , out );
    Where += 2;
  }

  /* write replen */
  /* have to halve these :( */
  Whatever[128] = 0x01;
  for ( i=0 ; i<16 ; i++ )
  {
    fseek ( out , 48+i*30 , 0 );
    j = SampleSizes[i] - LoopStart[i];
    if ( (j == 0) || (LoopStart[i] == 0) )
    {
      fwrite ( &Whatever[127] , 2 , 1 , out );
      continue;
    }
   
    j /= 2;
    /* use of htonl() suggested by Xigh !.*/
    k = htonl(j);
    Whatever[0] = *((Uchar *)&k+2);
    Whatever[1] = *((Uchar *)&k+3);
    fwrite ( Whatever , 2 , 1 , out );
  }


  /* fill replens up to 31st sample wiz $0001 */
  Whatever[49] = 0x01;
  for ( i=16 ; i<31 ; i++ )
  {
    fseek ( out , 48+i*30 , 0 );
    fwrite ( &Whatever[48] , 2 , 1 , out );
  }

  /* that's it for the samples ! */
  /* now, the pattern list */

  /* read number of pattern to play */
  fseek ( out , 950 , 0 );
  /* bypass empty byte (saved wiz a WORD ..) */
  Where += 1;
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* write ntk byte */
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );

  /* read/write pattern list */
  for ( i=0 ; i<40 ; i++ )
  {
    Where += 1;
    fwrite ( &in_data[Where++] , 1 , 1 , out );
  }


  /* write ptk's ID */
  fseek ( out , 0 , 2 );
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );



  /* now, the pattern data */

  /* bypass the "SONG" ID */
  Where += 4;

  /* read pattern data size */
  j = ((in_data[Where]*256*256*256)+
       (in_data[Where+1]*256*256)+
       (in_data[Where+2]*256)+
        in_data[Where+3] );
  Where += 4;
 
  /* read pattern data */
  free ( Whatever );
  Whatever = (Uchar *) malloc ( j );

  /* convert shits */
  for ( i=0 ; i<j ; i+=4 )
  {
    Whatever[i]   = in_data[Where++];
    Whatever[i+1] = in_data[Where++];
    Whatever[i+2] = in_data[Where++];
    Whatever[i+3] = in_data[Where++];
    /* convert fx C arg back to hex value */
    if ( (Whatever[i+2]&0x0f) == 0x0c )
    {
      c1 = Whatever[i+3];
      if ( c1 <= 9 ) { Whatever[i+3] = c1; continue; }
      if ( (c1 >= 16) && (c1 <= 25) ) { Whatever[i+3] = (c1-6); continue; }
      if ( (c1 >= 32) && (c1 <= 41) ) { Whatever[i+3] = (c1-12); continue; }
      if ( (c1 >= 48) && (c1 <= 57) ) { Whatever[i+3] = (c1-18); continue; }
      if ( (c1 >= 64) && (c1 <= 73) ) { Whatever[i+3] = (c1-24); continue; }
      if ( (c1 >= 80) && (c1 <= 89) ) { Whatever[i+3] = (c1-30); continue; }
      if ( (c1 >= 96) && (c1 <= 100)) { Whatever[i+3] = (c1-36); continue; }
/*      printf ( "error:vol arg:%x (at:%ld)\n" , c1 , i+200 );*/
    }
  }

  /* write pattern data */
  fwrite ( Whatever , j , 1 , out );
  free ( Whatever );

  /* read/write sample data */
  Where += 4;
  for ( i=0 ; i<16 ; i++ )
  {
    if ( SampleSizes[i] != 0 )
    {
      fwrite ( &in_data[Where] , SampleSizes[i] , 1 , out );
      Where += SampleSizes[i];
    }
  }


  /* crap */
  Crap ( "  Fuchs Tracker   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
