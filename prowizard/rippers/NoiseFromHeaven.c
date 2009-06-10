/* testNFH() */
/* Rip_NFH() */
/* Depack_NFH() */

#include "globals.h"
#include "extern.h"


/* Noise from Heaven Chipdisk (21 oct 2001) by Iris */

short testNFH ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  PW_WholeSampleSize = 0;
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

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+950];
  if ( (PW_l>127) || (PW_l==0) )
  {
    /*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+952+PW_j];
    if ( in_data[PW_Start_Address+952+PW_j] > 127 )
    {
      /*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* found some obscure ptk :( */
  while ( PW_j < 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > 0x7f )
    {
      /*printf ( "#4,2 (Start:%ld) (PW_j:%ld) (at:%ld)\n" , PW_Start_Address,PW_j ,PW_Start_Address+952+PW_j );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+1084+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}



/* noise from heaven chiptune mag by Iris'01 */

void Rip_NFH ( void )
{
  /* PW_WholeSampleSize id still the whole sample size */

  PW_l=0;
  for ( PW_k=0 ; PW_k<128 ; PW_k++ )
    if ( in_data[PW_Start_Address+952+PW_k] > PW_l )
      PW_l = in_data[PW_Start_Address+952+PW_k];
  PW_l += 1;
  OutputSize = (PW_l*1024) + 1084 + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bProrunner 1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[ProRunner_v1][0];
  OutName[2] = Extensions[ProRunner_v1][1];
  OutName[3] = Extensions[ProRunner_v1][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Noise From Heaven module", NoiseFromHeaven );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1083); /* 1080 could be enough */
}



/*
 *   nfh.c   2003 (c) Asle / ReDoX
 *
 * converts ziks from Noise From Heaven chiptune diskmag by Iris'01
 *
*/
void Depack_NFH ( void )
{
  Uchar *Whatever;
  Uchar poss[37][2];
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where=PW_Start_Address;
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read and write whole header */
  fwrite ( &in_data[Where] , 1080 , 1 , out );

  /* get whole sample size */
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  Where += 952 /* after size of pattern list .. before pattern list itself */;

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
      Whatever[2] |= (in_data[Where+2]/2);
      Whatever[3] = in_data[Where+3];
      Whatever[0] |= poss[(in_data[Where+1]/2)][0];
      Whatever[1] = poss[(in_data[Where+1]/2)][1];
      fwrite ( Whatever , 4 , 1 , out );
      Where += 4;
    }
  }
  free ( Whatever );


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "Noise From Heaven " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

