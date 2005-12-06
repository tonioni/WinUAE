/*
 *   ProRunner1.c   1996 (c) Asle / ReDoX
 *
 * Converts MODs converted with Prorunner packer v1.0
 *
 * update:28/11/99
 *   - removed fopen() and all similar functions
 *   - Speed and Size (1/4) optimizings
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_PRUN1 ( void )
{
  Uchar *Whatever;
  Uchar poss[37][2];
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where=PW_Start_Address;
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

  /* read and write whole header */
  fwrite ( &in_data[Where] , 950 , 1 , out );

  /* get whole sample size */
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write size of pattern list */
  /* read and write ntk byte and pattern list */
  fwrite ( &in_data[Where+950] , 130 , 1 , out );
  Where += 952;

  /* write ID */
  Whatever = (Uchar *) malloc (4);
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

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
  for ( i=0 ; i<=Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Whatever[0] = in_data[Where] & 0xf0;
      Whatever[2] = (in_data[Where] & 0x0f)<<4;
      Whatever[2] |= in_data[Where+2];
      Whatever[3] = in_data[Where+3];
      Whatever[0] |= poss[in_data[Where+1]][0];
      Whatever[1] = poss[in_data[Where+1]][1];
      fwrite ( Whatever , 4 , 1 , out );
      Where += 4;
    }
  }
  free ( Whatever );


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "   ProRunner v1   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

