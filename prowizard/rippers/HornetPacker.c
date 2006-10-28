/* testHRT() */
/* Rip_HRT() */
/* Depack_HRT() */

#include "globals.h"
#include "extern.h"


short testHRT ( void )
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
    if ( in_data[45+PW_j*30+PW_Start_Address] > 0x40 )
    {
      return BAD;
    }
  }

  return GOOD;
}



void Rip_HRT ( void )
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
  PW_k = 1084 + PW_l * 1024;
  OutputSize = PW_k + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bHORNET packed module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "HORNET packed module", Hornet_packer );
  
  if ( Save_Status == GOOD )
    PW_i += 1084;
}


/*
 *   Hornet_Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts MODs converted with Hornet packer
 * GCC Hornet_Packer.c -o Hornet_Packer -Wall -O3
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

void Depack_HRT ( void )
{
  Uchar *Whatever;
  Uchar poss[37][2];
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read header */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  for ( i=0 ; i<950 ; i++ )
    Whatever[i] = in_data[Where++];


  /* empty-ing those adresse values ... */
  for ( i=0 ; i<31 ; i++ )
  {
    Whatever[38+(30*i)] = 0x00;
    Whatever[38+(30*i)+1] = 0x00;
    Whatever[38+(30*i)+2] = 0x00;
    Whatever[38+(30*i)+3] = 0x00;
  }

  /* write header */
  fwrite ( Whatever , 950 , 1 , out );

  /* get whole sample size */
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((Whatever[42+(30*i)]*256)+Whatever[43+30*i])*2);
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* number of pattern */
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* read noisetracker byte and pattern list */
  Where += 1;
  Whatever[256] = 0x7f;
  fwrite ( &in_data[256] , 1 , 1 , out );
  fwrite ( &in_data[Where] , 128 , 1 , out );

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i] > Max )
      Max = in_data[Where+i];
  }
  /*printf ( "Number of pattern : %d\n" , Max );*/

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  Where = PW_Start_Address + 1084;
  for ( i=0 ; i<=Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Whatever[0] = in_data[Where];
      Whatever[1] = in_data[Where+1];
      Whatever[2] = in_data[Where+2];
      Whatever[3] = in_data[Where+3];
      Whatever[0] /= 2;
      Whatever[16] = Whatever[0] & 0xf0;
      if ( Whatever[1] == 0x00 )
        Whatever[17] = 0x00;
      else
      {
        Whatever[16] |= poss[(Whatever[1]/2)][0];
        Whatever[17] = poss[(Whatever[1]/2)][1];
      }
      Whatever[18] = (Whatever[0]<<4)&0xf0;
      Whatever[18] |= Whatever[2];
      Whatever[19] = Whatever[3];

      fwrite ( &Whatever[16] , 4 , 1 , out );
      Where += 4;
    }
  }
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "  Hornet Packer   " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

