/* testFC_M() */
/* Rip_FC_M() */
/* Depack_FC_M() */

#include "globals.h"
#include "extern.h"


short testFC_M ( void )
{
  /* test 1 */
  PW_Start_Address = PW_i;
  if ( in_data[PW_Start_Address + 4] != 0x01 )
  {
    return BAD;
  }

  /* test 2 */
  if ( in_data[PW_Start_Address + 5] != 0x00 )
  {
    return BAD;
  }

  /* test 3 */
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+37+8*PW_j] > 0x40 )
    {
      return BAD;
    }
  }

  return GOOD;
}

void Rip_FC_M ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+34+PW_k*8]*256)+in_data[PW_Start_Address+35+PW_k*8])*2);
  PW_j = in_data[PW_Start_Address+286];
  PW_l = 0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+292+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+292+PW_k];
  }
  PW_l += 1;
  OutputSize = (PW_l*1024) + PW_WholeSampleSize + 292 + PW_j + 8;
  /*  printf ( "\b\b\b\b\b\b\b\bFC-M packed module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "FC-M packed module", FC_M_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1);  /* 0 should do but call it "just to be sure" :) */
}



/*
 *   FC-M_Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk FC-M packed MODs
 *
 * Last update: 28/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 * 20051002 : testing fopen()
*/
void Depack_FC_M ( void )
{
  Uchar *Whatever;
  long i=0;
  long WholeSampleSize=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* bypass "FC-M" ID */
  /* bypass what looks like the version number .. */
  /* bypass "NAME" chunk */
  /*Where += 10;*/

  /* read and write title */
  fwrite ( &in_data[Where+10] , 20 , 1 , out );
  /* bypass "INST" chunk */
  Where += 34;

  /* read and write sample descriptions */
  Whatever = (Uchar *)malloc(256);
  BZERO ( Whatever , 256 );
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 6 , 1 , out );
    Whatever[32] = in_data[Where+7];
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )
      Whatever[32] = 0x01;
    fwrite ( &in_data[Where+6] , 1 , 1 , out );
    fwrite ( &Whatever[32] , 1 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* bypass "LONG" chunk */
  Where += 4;

  /* read and write pattern table lenght */
  Whatever[128] = in_data[Where++];
  fwrite ( &Whatever[128] , 1 , 1 , out );
  /*printf ( "Size of pattern list : %d\n" , Whatever[128] );*/

  /* read and write NoiseTracker byte */
  fwrite ( &in_data[Where] , 1 , 1 , out );

  /* bypass "PATT" chunk */
  Where += 5;

  /* read and write pattern list and get highest patt number */
  for ( i=0 ; i<Whatever[128] ; i++ )
  {
    Whatever[i] = in_data[Where];
    if ( in_data[Where] > Whatever[129] )
      Whatever[129] = in_data[Where];
    Where += 1;
  }
  fwrite ( Whatever , 128 , 1 , out );
  /*printf ( "Number of pattern : %d\n" , Whatever[129] + 1 );*/

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* bypass "SONG" chunk */
  Where += 4;

  /* pattern data */
  for ( i=0 ; i<=Whatever[129] ; i++ )
  {
    fwrite ( &in_data[Where] , 1024 , 1 , out );
    Where += 1024;
    /*printf ( "+" );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/


  /* bypass "SAMP" chunk */
  Where += 4;

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "   FC-M packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
