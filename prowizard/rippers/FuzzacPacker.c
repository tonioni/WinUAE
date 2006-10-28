/* testFUZZAC() */
/* Rip_Fuzzac() */
/* Depack_Fuzzac() */

#include "globals.h"
#include "extern.h"


short testFUZZAC ( void )
{
  PW_Start_Address = PW_i;

  /* test finetune */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+72+PW_k*68] > 0x0F )
    {
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+73+PW_k*68] > 0x40 )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+66+PW_k*68]*256)+in_data[PW_Start_Address+67+PW_k*68];
    if ( PW_j > 0x8000 )
    {
      return BAD;
    }
    PW_WholeSampleSize += (PW_j * 2);
  }

  /* test size of pattern list */
  if ( in_data[PW_Start_Address+2114] == 0x00 )
  {
    return BAD;
  }

  return GOOD;
}



void Rip_Fuzzac ( void )
{
  /* PW_WholeSampleSize IS still the whole sample size */

  PW_j = in_data[PW_Start_Address+2114];
  PW_k = in_data[PW_Start_Address+2115];
  OutputSize = PW_WholeSampleSize + (PW_j*16) + (PW_k*256) + 2118 + 4;

  CONVERT = GOOD;
  Save_Rip ( "Fuzzac packer module", Fuzzac );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* -1 should do but call it "just to be sure" :) */
}

/*
 *   fuzzac.c   1997 (c) Asle / ReDoX
 *
 * Converts Fuzzac packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * Note: A most worked-up prog ... took some time to finish this !.
 *      there's what lot of my other depacker are missing : the correct
 *      pattern order (most of the time the list is generated badly ..).
 *      Dont know why I did it for this depacker because I've but one
 *      exemple file ! :).
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 *   - memory leak bug corrected (thx to Thomas Neumann)
 *   - SEnd ID bypassed REALLY now :) (Thomas Neumann again !)
*/


#define ON  1
#define OFF 2


void Depack_Fuzzac ( void )
{
  Uchar c5;
  Uchar PatPos;
  Uchar *Whatever;
  Uchar NbrTracks;
  Uchar Track_Numbers[128][16];
  Uchar Track_Numbers_Real[128][4];
  Uchar Track_Datas[4][256];
  Uchar Status=ON;
  long WholeSampleSize=0;
  long i,j,k,l;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Numbers , 128*16 );
  BZERO ( Track_Numbers_Real , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* bypass ID */
  /* bypass 2 unknown bytes */
  Where += 6;

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /*printf ( "Converting header ... " );*/
  /*fflush ( stdout );*/
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( &in_data[Where] , 22 , 1 , out );

    WholeSampleSize += (((in_data[Where+60]*256)+in_data[Where+61])*2);
    fwrite ( &in_data[Where+60] , 2 , 1 , out );
    fwrite ( &in_data[Where+66] , 2 , 1 , out );
    fwrite ( &in_data[Where+62] , 2 , 1 , out );

    Whatever[0] = in_data[Where+65];
    if ( (in_data[Where+64]==0x00) && (in_data[Where+65]==0x00) )
      Whatever[0] = 0x01;
    fwrite ( &in_data[Where+64] , 1 , 1 , out );
    fwrite ( Whatever , 1 , 1 , out );
    Where += 68;
  }
  /*printf ( "ok\n" );*/
  /*printf ( " - Whole sample size : %ld\n" , WholeSampleSize );*/

  /* read & write size of pattern list */
  PatPos = in_data[Where++];
  fwrite ( &PatPos , 1 , 1 , out );
  /*printf ( " - size of pattern list : %d\n" , PatPos );*/

  /* read the number of tracks */
  NbrTracks = in_data[Where++];

  /* write noisetracker byte */
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );


  /* place file pointer at track number list address */
  Where = PW_Start_Address + 2118;

  /* read tracks numbers */
  for ( i=0 ; i<4 ; i++ )
  {
    for ( j=0 ; j<PatPos ; j++ )
    {
      Track_Numbers[j][i*4]   = in_data[Where++];
      Track_Numbers[j][i*4+1] = in_data[Where++];
      Track_Numbers[j][i*4+2] = in_data[Where++];
      Track_Numbers[j][i*4+3] = in_data[Where++];
    }
  }

  /* sort tracks numbers */
  c5 = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i == 0 )
    {
      Whatever[0] = c5;
      c5 += 0x01;
      continue;
    }
    for ( j=0 ; j<i ; j++ )
    {
      Status = ON;
      for ( k=0 ; k<4 ; k++ )
      {
        if ( Track_Numbers[j][k*4] != Track_Numbers[i][k*4] )
        {
          Status=OFF;
          break;
        }
      }
      if ( Status == ON )
      {
        Whatever[i] = Whatever[j];
        break;
      }
    }
    if ( Status == OFF )
    {
      Whatever[i] = c5;
      c5 += 0x01;
    }
    Status = ON;
  }
  /* c5 is the Max pattern number */


  /* create a real list of tracks numbers for the really existing patterns */
  Whatever[129] = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i==0 )
    {
      Track_Numbers_Real[Whatever[129]][0] = Track_Numbers[i][0];
      Track_Numbers_Real[Whatever[129]][1] = Track_Numbers[i][4];
      Track_Numbers_Real[Whatever[129]][2] = Track_Numbers[i][8];
      Track_Numbers_Real[Whatever[129]][3] = Track_Numbers[i][12];
      Whatever[129] += 0x01;
      continue;
    }
    for ( j=0 ; j<i ; j++ )
    {
      Status = ON;
      if ( Whatever[i] == Whatever[j] )
      {
        Status = OFF;
        break;
      }
    }
    if ( Status == OFF )
      continue;
    Track_Numbers_Real[Whatever[129]][0] = Track_Numbers[i][0];
    Track_Numbers_Real[Whatever[129]][1] = Track_Numbers[i][4];
    Track_Numbers_Real[Whatever[129]][2] = Track_Numbers[i][8];
    Track_Numbers_Real[Whatever[129]][3] = Track_Numbers[i][12];
    Whatever[129] += 0x01;
    Status = ON;
  }

  /* write pattern list */
  fwrite ( Whatever , 128 , 1 , out );

  /* write ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* pattern data */
  /*printf ( "Processing the pattern datas ... " );*/
  /*fflush ( stdout );*/
  l = PW_Start_Address + 2118 + (PatPos * 16);
  for ( i=0 ; i<c5 ; i++ )
  {
    BZERO ( Whatever , 1024 );
    BZERO ( Track_Datas , 4*256 );
    Where = l + (Track_Numbers_Real[i][0]*256);
    for ( j=0 ; j<256 ; j++ ) Track_Datas[0][j] = in_data[Where+j];
    Where = l + (Track_Numbers_Real[i][1]*256);
    for ( j=0 ; j<256 ; j++ ) Track_Datas[1][j] = in_data[Where+j];
    Where = l + (Track_Numbers_Real[i][2]*256);
    for ( j=0 ; j<256 ; j++ ) Track_Datas[2][j] = in_data[Where+j];
    Where = l + (Track_Numbers_Real[i][3]*256);
    for ( j=0 ; j<256 ; j++ ) Track_Datas[3][j] = in_data[Where+j];

    for ( j=0 ; j<64 ; j++ )
    {
      Whatever[j*16]    = Track_Datas[0][j*4];
      Whatever[j*16+1]  = Track_Datas[0][j*4+1];
      Whatever[j*16+2]  = Track_Datas[0][j*4+2];
      Whatever[j*16+3]  = Track_Datas[0][j*4+3];
      Whatever[j*16+4]  = Track_Datas[1][j*4];
      Whatever[j*16+5]  = Track_Datas[1][j*4+1];
      Whatever[j*16+6]  = Track_Datas[1][j*4+2];
      Whatever[j*16+7]  = Track_Datas[1][j*4+3];
      Whatever[j*16+8]  = Track_Datas[2][j*4];
      Whatever[j*16+9]  = Track_Datas[2][j*4+1];
      Whatever[j*16+10] = Track_Datas[2][j*4+2];
      Whatever[j*16+11] = Track_Datas[2][j*4+3];
      Whatever[j*16+12] = Track_Datas[3][j*4];
      Whatever[j*16+13] = Track_Datas[3][j*4+1];
      Whatever[j*16+14] = Track_Datas[3][j*4+2];
      Whatever[j*16+15] = Track_Datas[3][j*4+3];
    }

    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
    /*fflush ( stdout );*/
  }
  free ( Whatever );
  /*printf ( "ok\n" );*/

  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  /*fflush ( stdout );*/
  Where = l + 4 + NbrTracks*256;
  /* l : 2118 + NumberOfPattern*16+PW_Start_Address */
  /* 4 : to bypass the "SEnd" unidentified ID */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );
  /*printf ( "ok\n" );*/
  

  /* crap ... */
  Crap ( "  FUZZAC Packer   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
