/* update on the 3rd of april 2000 */
/* some advices by Thomas Neumann .. thx */

/* testPM() */
/* Rip_PM() */
/* Depack_PM() */

#include "globals.h"
#include "extern.h"


/* Power Music */
short testPM ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+45+30*PW_j] > 0x40 )
    {
      return BAD;
    }
  }

  /* test 3 */
  if ( (in_data[PW_Start_Address+951] > 0x7F) && (in_data[PW_Start_Address+951] != 0xFF) )
  {
    return BAD;
  }

  return GOOD;
}



void Rip_PM ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);

  PW_j = in_data[PW_Start_Address+950];
  PW_l=0;
  for ( PW_k=0 ; PW_k<128 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+952+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+952+PW_k];
  }
  PW_l += 1;
  PW_k = 1084 + (PW_l * 1024);
  OutputSize = PW_k + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "Power Music module", Power_Music );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1083);  /* 1080 should do but call it "just to be sure" :) */
}



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
  out = PW_fopen ( Depacked_OutName , "w+b" );

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

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
