/* testBNR()    */
/* Rip_BNR()    */
/* Depack_BNR() */

#include "globals.h"
#include "extern.h"


short testBNR ( void )
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
    /* loop size */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop start */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps(PW_j*2, PW_n, PW_m, in_data[PW_Start_Address+45+30*PW_k], in_data[PW_Start_Address+44+30*PW_k] ) == BAD )
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
    /*printf ( "#5,0 (Start:%ld)(1patsize:%ld)\n" , PW_Start_Address, 1024);*/
    return BAD;
  }

  return GOOD;
}


/* Rip_AC1D */
void Rip_BNR ( void )
{
  /* PW_WholeSampleSize still hold the whole sample size */
  /* PW_k is still the number of pattern stored */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  CONVERT = GOOD;
  Save_Rip ( "Binary Packer module", BNR );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}



/*
 *   bnr.c   1999 (c) Asle / ReDoX
 * 20071223 - added into PW at Muerto's request
*/
void Depack_BNR ( void )
{
  uint8_t c1,c2;
  uint8_t *Whatever;
  uint8_t Nbr_Pat=0;
  uint8_t poss[37][2];
  int32_t WholeSampleSize=0;
  int32_t i,j;
  int32_t Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  /*printf ("\nWhere:%ld\n", Where);*/

  /* read header */
  Whatever = (uint8_t *) malloc (1084);
  BZERO ( Whatever , 1084 );
  for ( i=0 ; i<1084 ; i++ )
  {
    Whatever[i] = in_data[i+Where];
  }

  /* reorder loops  and sample size */
  for (i=0 ; i<31 ; i++)
  {
    c1 = Whatever[i*30+20+26];
    c2 = Whatever[i*30+20+27];
    Whatever[i*30+20+26] = Whatever[i*30+20+28];
    Whatever[i*30+20+27] = Whatever[i*30+20+29];
    Whatever[i*30+20+28] = c1;
    Whatever[i*30+20+29] = c2;
    WholeSampleSize += (((Whatever[i*30+20+22]*256)+Whatever[i*30+20+23])*2);
  }
  
  /* M.K. ... */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';
  
  /* write header */
  fwrite (Whatever,1084,1,out);
  Where += 1084;

  /* get number of patterns */
  for (i=952; i<1080 ; i++)
  {
    if (Whatever[i] > Nbr_Pat)
      Nbr_Pat = Whatever[i];
  }
  Nbr_Pat += 1;

  /* pattern data */
  for ( i=0 ; i<Nbr_Pat ; i++ )
  {
     BZERO (Whatever,1084);
    for ( j=0 ; j<256 ; j++ )
    {
      /* Fx Arg */
      Whatever[j*4+3]  = in_data[Where+i*1024+j*4+2];
      /* Fx */
      Whatever[j*4+2]  = (in_data[Where+i*1024+j*4]>>2)&0x0f;
      /* Smp Number */
      Whatever[j*4+2] |= (in_data[Where+i*1024+j*4+3]<<1)&0xf0;
      Whatever[j*4]    = (in_data[Where+i*1024+j*4+3]>>3)&0xf0;

      /* test flag #2 */
      if ( (in_data[Where+i*1024+j*4] & 0x40) == 0x40 )
        continue;
      /* test flag #1 */
      if ( (in_data[Where+i*1024+j*4] & 0x80) == 0x80 )
      {
/*        printf ( "!!! at : %ld, Fx '%x' was converted to 0x0F\n"
                 , var+i*1024+j*4
                 , WholeFile[var+i*1024+j*4] );*/
        Whatever[j*4+2] |= 0x0f;  /* forcing set BPM */
      }

      /* notes ... */
      Whatever[j*4]   |= poss[37-(in_data[Where+i*1024+j*4+1]>>1)][0]&0x0f;
      Whatever[j*4+1]  = poss[37-(in_data[Where+i*1024+j*4+1]>>1)][1];

    }
    fwrite ( Whatever , 1024 , 1 , out );
  }

  free ( Whatever );

  /* sample data */
  Where += (Nbr_Pat*1024);
  /*for (i = Where; i<WholeSampleSize+Where ; i++)in_data[i]-=0x80;*/
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* no crap() as it overwrites sample text */
  /*Crap ( "  Binary Packer   " , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
