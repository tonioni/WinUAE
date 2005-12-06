/*
 *   Module_Protector.c   1997 (c) Asle / ReDoX
 *
 * Converts MP packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 *  NOTE : It takes care of both MP packed files with or without ID !
 *
 * Last update: 28/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_MP ( void )
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

  if ( (in_data[Where] == 'T') && (in_data[Where+1] == 'R') && (in_data[Where+2] == 'K') && (in_data[Where+3] == '1') )
    Where += 4;

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
  fwrite ( &in_data[Where] , 2 , 1 , out );
  Where += 2;

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

  /* bypass 4 unknown empty bytes */
  if ( (in_data[Where]==0x00)&&(in_data[Where+1]==0x00)&&(in_data[Where+2]==0x00)&&(in_data[Where+3]==0x00) )
    Where += 4;
  /*else*/
    /*printf ( "! four empty bytes bypassed at the beginning of the pattern data\n" );*/

  /* pattern data */
  i = (Whatever[32]+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " Module Protector " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
