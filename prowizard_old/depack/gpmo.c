/*
 *   gpmo.c   2003 (c) Asle / ReDoX
 *
 * Converts GPMO MODs back to PTK
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

void Depack_GPMO ( void )
{
  Uchar *Whatever;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0;
  long Where=PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* get whole sample size and patch vols (/2)*/
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
    in_data[Where+45+i*30] = in_data[Where+45+i*30]/2;
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write whole header */
  fwrite ( &in_data[Where] , 1080 , 1 , out );

  Where += 952;

  /* write ID */
  Whatever = (Uchar *) malloc (4);
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );
  free ( Whatever );

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i] > Max )
      Max = in_data[Where+i];
  }
  /*printf ( "Number of pattern : %d\n" , Max );*/

  /* pattern data */
  Where = PW_Start_Address + 1084;
  fwrite ( &in_data[Where], (Max+1)*1024, 1, out);
  Where += ((Max+1)*1024);

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "       GPMO       " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

