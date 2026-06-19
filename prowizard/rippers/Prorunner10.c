/* testPRUN1() */
/* Rip_PRUN1() */
/* Depack_PRUN1() */


#include "globals.h"
#include "extern.h"


int16_t	 testPRUN1 ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  if ( (in_data[PW_Start_Address+951] != 0x7f) && (in_data[PW_Start_Address+951] != 0x00) )
  {
    return BAD;
  }

  /* test 3 */
  if ( in_data[PW_Start_Address+950] > 0x7f )
  {
    return BAD;
  }

  /* test 4 */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps(PW_j*2, PW_m, PW_n, in_data[PW_Start_Address+45+30*PW_k], in_data[PW_Start_Address+44+30*PW_k] ) == BAD )
    {
      /*printf ( "start : %ld\n", PW_Start_Address );*/
      return BAD; 
    }

    PW_WholeSampleSize += PW_j;
  }

  return GOOD;
}



void Rip_PRUN1 ( void )
{
  PW_l=0;
  for ( PW_k=0 ; PW_k<128 ; PW_k++ )
    if ( in_data[PW_Start_Address+952+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+952+PW_k];
  PW_l += 1;
  OutputSize = (PW_l*1024) + 1084 + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "Prorunner 1 module", ProRunner_v1 );
  
  if ( Save_Status == GOOD )
    PW_i += 1; /* 1080 could be enough */
}



/*
 *   ProRunner1.c   1996 (c) Asle / ReDoX
 *
 * Converts MODs converted with Prorunner packer v1.0
 *
 * update:28/11/99
 *   - removed fopen() and all similar functions
 *   - Speed and Size (1/4) optimizings
*/
void Depack_PRUN1 ( void )
{
  uint8_t *Whatever;
  uint8_t poss[37][2];
  uint8_t Max=0x00;
  int32_t	 WholeSampleSize=0;
  int32_t	 i=0,j=0;
  int32_t	 Where=PW_Start_Address;
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

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
  Whatever = (uint8_t *) malloc (4);
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

