/*
 *   PowerMusic.c   1996 (c) Asle / ReDoX
 *
 * Converts back to ptk Optimod's power music files
 *
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up and Binary smaller.
 * update: 01/12/99
 *   - removed fopen() and attached funcs
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_PM ( void )
{
  signed char *Smp_Data;
  Uchar c1=0x00,c2=0x00;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* write whole header */
  fwrite ( &in_data[Where] , 950 , 1 , out );

  /* get whole sample size */
  for ( i=0 ; i<31 ; i++ )
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write size of pattern list */
  fwrite ( &in_data[Where+950] , 1 , 1 , out );

  /* read and write ntk byte and pattern list */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &in_data[Where+952] , 128 , 1 , out );

  /* write ID */
  c1 = 'M';
  c2 = '.';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  c1 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i+952] > Max )
      Max = in_data[Where+i+952];
  }
  j = Max += 1;
  /*printf ( "\nNumber of pattern : %ld\n" , j );*/
  /* pattern data */
  j *= 1024;
  fwrite ( &in_data[Where+1084] , j , 1 , out );
  j += 1084;

  /* sample data */
  Smp_Data = (signed char *) malloc ( WholeSampleSize );
  BZERO ( Smp_Data , WholeSampleSize );
  Smp_Data[0] = in_data[Where+j];
  for ( i=1 ; i<WholeSampleSize-1 ; i++ )
  {
    Smp_Data[i] = Smp_Data[i-1] + (signed char)in_data[Where+j+i];
  }
  fwrite ( Smp_Data , WholeSampleSize , 1 , out );
  free ( Smp_Data );


  /* crap */
  Crap ( "   Power Music    " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[Power Music to]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[Protracker]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
