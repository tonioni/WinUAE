/* testHCD() */
/* Rip_HCD() */
/* Depack_HCD() */


#include "globals.h"
#include "extern.h"


int16_t	 testHCD ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
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
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2)-0x68A;
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps(PW_j*2, PW_m, PW_n, in_data[PW_Start_Address+45+30*PW_k], in_data[PW_Start_Address+44+30*PW_k] ) == BAD )
    {
      /*printf ( "start : %ld\n", PW_Start_Address );*/
      return BAD; 
    }

    PW_WholeSampleSize += PW_j;
  }
  
  /* test #3  pattern list size */
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
  PW_k += 1;

  return GOOD;
}



void Rip_HCD ( void )
{
  OutputSize = (PW_k*1024) + 1084 + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "HCD-Protector module", HCD );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}



/*
 *   HCD.c   2008 (c) Sylvain "Asle" Chipaux
 
 same header as PTK save for the sample loop start which is +0x0345
 pattern data is <<3
*/
void Depack_HCD ( void )
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

  /* header */
  Whatever = (uint8_t *) malloc (2048);
  for ( i=0; i<1084 ; i++ )
  {
    Whatever[i] = in_data[Where+i];
  }
  
  /* fix loop start */
  for (i=46 ; i<950 ; i+=30)
  {
    Whatever[i] -= 0x03;
    Whatever[i+1] -= 0x45;
    WholeSampleSize += (((in_data[Where+i-4]*256)+in_data[Where+i-3])*2);
  }
  
  /*write header*/
  fwrite ( Whatever , 1084 , 1 , out );

  /* get number of pattern */
  Max = 0x00;
  for ( i=952 ; i<1080 ; i++ )
  {
    if ( Whatever[i] > Max )
      Max = Whatever[i];
  }
  /*printf ( "Number of pattern : %d\n" , Max );*/

  /* pattern data */
  Where += 1084;
  for ( i=0 ; i<=Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
        /*d = ((uint32_t	 *) in_data)[0] >> 3;*/
      Whatever[j*4] = in_data[Where]>>3;
      Whatever[j*4+1] = (in_data[Where+1]>>3)|(in_data[Where]<<5);
      Whatever[j*4+2] = (in_data[Where+2]>>3)|(in_data[Where+1]<<5);
      Whatever[j*4+3] = (in_data[Where+3]>>3)|(in_data[Where+2]<<5);
      Where += 4;
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );


  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "  HCD Protector   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

