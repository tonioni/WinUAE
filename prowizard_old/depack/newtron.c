/*
 *   newtron.c   2003 (c) Asle / ReDoX
 *
 * Converts Newtron packed MODs back to PTK MODs
 *
 * Last update: 09 mar 2003
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_Newtron ( void )
{
  Uchar *Whatever;
  long i=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  Whatever = (Uchar *) malloc (64);
  BZERO ( Whatever , 64 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  if (!out)
      return;
  out = mr_fopen ( Depacked_OutName , "w+b" );

  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  Where = 4;

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
  fwrite ( &in_data[0] , 1 , 1 , out );
  Whatever[0] = 0x7f;
  fwrite ( &Whatever[0] , 1 , 1 , out );

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

  /* pattern data */
  i = (Whatever[32]+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "      Newtron     " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
