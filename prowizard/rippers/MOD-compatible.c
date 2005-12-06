/* testMOD() */
/* Rip_MOD() */

/*
 this funtion will handle any MOD-alike formats
 meaning whatever the number of channels. This
 number is the arg in fact.
*/


#include "globals.h"
#include "extern.h"


short testMOD ( int nbrchannels )
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
  if ( ((PW_k*256*nbrchannels)+1084+PW_Start_Address) > PW_in_size )
  {
    /*printf ( "#5,0 (Start:%ld)(1patsize:%ld)\n" , PW_Start_Address, 256*nbrchannels);*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* sample > 1f   or   pitch > 358 ? */
    if ( in_data[PW_Start_Address+1084+PW_j*nbrchannels] > 0x13 )
    {
      /*printf ( "#5.1 (Start:%ld)(sample value:%x)(Where:%lx)\n" , PW_Start_Address,in_data[PW_Start_Address+1084+PW_j*nbrchannels],PW_Start_Address+1084+PW_j*nbrchannels);*/
      return BAD;
    }
    PW_l = ((in_data[PW_Start_Address+1084+PW_j*nbrchannels]&0x0f)*256)+in_data[PW_Start_Address+1085+PW_j*nbrchannels];
    if ( (PW_l>0) && (PW_l<0x1C) )
    {
      /*printf ( "#5,2 (Start:%ld)(PW_l:%lx)(Where:%lx)\n" , PW_Start_Address,PW_l,PW_Start_Address+1084+PW_j*nbrchannels );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_MOD ( int nbrchannels )
{
  char whatever[64];
  BZERO (whatever,64);
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is still the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_k*256*nbrchannels) + 1084;

  CONVERT = BAD;
  sprintf(whatever,"Pro|Star|whatever tracker %d channels module",nbrchannels);
  Save_Rip ( whatever, Protracker );
  
  if ( Save_Status == GOOD )
    PW_i += 1084;
}

