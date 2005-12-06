/*
 *   ProPacker_v1.0   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk ProPacker v1 MODs
 *
 * Update: 28/11/99
 *     - removed fopen() and all attached functions.
 *     - overall speed and size optimizings.
 * Update: 19/04/00 (all pointed out by Thomas Neumann)
 *     - replen bug correction
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_PP10 ( void )
{
  Uchar Tracks_Numbers[4][128];
  Uchar Pat_Pos;
  Uchar *Whatever;
  short Max;
  long i=0,j=0,k=0;
  long WholeSampleSize=0;
  long Where=PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Tracks_Numbers , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 6 , 1 , out );

    Whatever[32] = in_data[Where+6];
    Whatever[33] = in_data[Where+7];
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )
      Whatever[33] = 0x01;
    fwrite ( &Whatever[32] , 2 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* read and write pattern table lenght */
  Pat_Pos = in_data[Where++];
  fwrite ( &Pat_Pos , 1 , 1 , out );
  /*printf ( "Size of pattern list : %d\n" , Pat_Pos );*/

  /* read and write NoiseTracker byte */
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* read track list and get highest track number */
  Max = 0;
  for ( j=0 ; j<4 ; j++ )
  {
    for ( i=0 ; i<128 ; i++ )
    {
      Tracks_Numbers[j][i] = in_data[Where++];
      if ( Tracks_Numbers[j][i] > Max )
	Max = Tracks_Numbers[j][i];
    }
  }
  /*printf ( "highest track number : %d\n" , Max+1 );*/

  /* write pattern table "as is" ... */
  for (Whatever[0]=0 ; Whatever[0]<Pat_Pos ; Whatever[0]+=0x01 )
    fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[256] , (128-Pat_Pos) , 1 , out );
  /* Where is reassigned later */

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* track/pattern data */

  for ( i=0 ; i<Pat_Pos ; i++ )
  {
/*fprintf ( info , "\n\n\nPattern %ld :\n" , i );*/
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      Where = PW_Start_Address + 762+(Tracks_Numbers[j][i]*256);
/*fprintf ( info , "Voice %ld :\n" , j );*/
      for ( k=0 ; k<64 ; k++ )
      {
        Whatever[k*16+j*4]   = in_data[Where++];
        Whatever[k*16+j*4+1] = in_data[Where++];
        Whatever[k*16+j*4+2] = in_data[Where++];
        Whatever[k*16+j*4+3] = in_data[Where++];
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/


  /* now, lets put file pointer at the beginning of the sample datas */
  Where = PW_Start_Address + 762 + ((Max+1)*256);

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "  ProPacker v1.0  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
