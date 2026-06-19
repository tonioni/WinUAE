/* testMOSH() */
/* Rip_MOSH() */
/* Depack_MOSH() */


#include "globals.h"
#include "extern.h"


int16_t	 testMOSH ( void )
{
  /* test 1 */
  if ( PW_i < 378 )
  {
    return BAD;
  }

  PW_Start_Address = PW_i-378;

  /* samples */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+6+PW_k*8]*256)+in_data[PW_Start_Address+7+PW_k*8])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+2+PW_k*8]*256)+in_data[PW_Start_Address+3+PW_k*8])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+PW_k*8]*256)+in_data[PW_Start_Address+1+PW_k*8])*2);

    if ( test_smps(PW_j*2, PW_m, PW_n, in_data[PW_Start_Address+5+8*PW_k], in_data[PW_Start_Address+4+8*PW_k] ) == BAD )
    {
      /*printf ( "start : %ld\n", PW_Start_Address );*/
      return BAD; 
    }

    PW_WholeSampleSize += PW_j;
  }

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+248];
  if ( (PW_l>127) || (PW_l==0) )
  {
    /*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+250+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+250+PW_j];
    if ( in_data[PW_Start_Address+250+PW_j] > 127 )
    {
      /*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+382+PW_Start_Address) > PW_in_size )
  {
    /*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address);*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* sample > 1f   or   pitch > 358 ? */
    if ( in_data[PW_Start_Address+382+PW_j*4] > 0x13 )
    {
      /*printf ( "#5.1 (Start:%ld)(sample value:%x)(Where:%lx)\n" , PW_Start_Address,in_data[PW_Start_Address+382+PW_j*4],PW_Start_Address+382+PW_j*4);*/
      return BAD;
    }
    PW_l = ((in_data[PW_Start_Address+382+PW_j*4]&0x0f)*256)+in_data[PW_Start_Address+383+PW_j*4];
    if ( (PW_l>0) && (PW_l<0x1C) )
    {
      /*printf ( "#5,2 (Start:%ld)(PW_l:%lx)(Where:%lx)\n" , PW_Start_Address,PW_l,PW_Start_Address+382+PW_j*4 );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_MOSH ( void )
{
  /* PW_k is still the number of patterns */
  
  OutputSize = (PW_k*1024) + 382 + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "MOSH packed module", MOSH );
  
  if ( Save_Status == GOOD )
    PW_i += 379; /* after the 'M' of M.K. */
}



/*
 *   MOSH.c   2007 (c) Asle
 *
*/
void Depack_MOSH ( void )
{
  uint8_t *Whatever;
  uint8_t Max=0x00;
  int32_t WholeSampleSize=0;
  int32_t i=0;
  int32_t Where=PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (uint8_t *) malloc (1024);
  BZERO (Whatever , 1024);
  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    /* loop size */
    Whatever[i*30+28] = in_data[Where];
    Whatever[i*30+29] = in_data[Where+1];
    /* loop start */
    Whatever[i*30+26] = in_data[Where+2];
    Whatever[i*30+27] = in_data[Where+3];
    /* fine */
    Whatever[i*30+24] = in_data[Where+4];
    /* volume */
    Whatever[i*30+25] = in_data[Where+5];
    /* size */
    Whatever[i*30+22] = in_data[Where+6];
    Whatever[i*30+23] = in_data[Where+7];
    WholeSampleSize += (((in_data[Where+6]*256)+in_data[Where+7])*2);

    Where += 8;
  }
  fwrite (Whatever,930,1,out);
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* write until after M.K. */
  fwrite ( &in_data[Where] , 134 , 1 , out );
  Where += 2; /* to be on at the patternlist level */
  
  /* getting highest pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    Max = ( in_data[Where] > Max ) ? in_data[Where] : Max;
    Where += 1;
  }
  Max += 1;
  Where += 4; /*M.K.*/
  /*printf ( "\nNumber of pattern : %d\n" , Max );*/
  fwrite ( &in_data[Where] , (Max*1024) , 1 , out );
  Where += (Max*1024); /*patdata*/

  /* pattern data */

  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "    Mosh Player   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

