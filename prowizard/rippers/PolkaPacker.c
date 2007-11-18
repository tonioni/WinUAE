/*  (Mar 2003)
 *    polka.c
*/
/* testPolka() */
/* Rip_Polka() */
/* Depack_Polka() */


#include "globals.h"
#include "extern.h"


short testPolka ( void )
{
  /* test #1 */
  if ( (PW_i < 0x438) || ((PW_i+0x830)>PW_in_size))
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-0x438;
  PW_l=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+42+30*PW_j]*256)+in_data[PW_Start_Address+43+30*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+30*PW_j]*256)+in_data[PW_Start_Address+47+30*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+30*PW_j]*256)+in_data[PW_Start_Address+49+30*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m/2, PW_n, in_data[PW_Start_Address+45+30*PW_j], in_data[PW_Start_Address+44+30*PW_j] ) == BAD )
    {
      /*      printf ("#2 (start:%ld)(siz:%ld)(lstart:%ld)(lsiz:%ld)(vol:%d)(fine:%d)(Where:%ld)\n"
	      ,PW_Start_Address,PW_k,PW_m,PW_n
	      ,in_data[PW_Start_Address+0x2d +30+PW_j],in_data[PW_Start_Address+0x2c +30*PW_j]
	      ,PW_Start_Address+0x2a + 30*PW_j);*/
      return BAD; 
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
    /*    printf(  "#2,1\n" );*/
    return BAD;
  }

  /* test #3 */
  PW_l = in_data[PW_Start_Address+0x3b6];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#3\n" );*/
    return BAD;
  }

  /* test #4 */
  /* PW_l contains the size of the pattern list */
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+0x3b8+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+0x3b8+PW_j];
    if ( in_data[PW_Start_Address+0x3b8+PW_j] > 0x7f )
    {
      /*printf(  "#4 (start:%ld)\n",PW_Start_Address );*/
      return BAD;
    }
  }
  PW_k += 1;

  /* test #5  notes .. gosh ! (testing all patterns !) */
  /* PW_k contains the number of pattern saved */
  /* PW_WholeSampleSize contains the whole sample size :) */
  for ( PW_j=0 ; PW_j<(256*PW_k) ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+0x43c+PW_j*4];
    if ( (PW_l > 72) || (((PW_l/2)*2)!=PW_l) )
    {
      /*printf(  "#5 (start:%ld)(note:%ld)(where:%ld)\n",PW_Start_Address,PW_l,PW_Start_Address+0x43c +PW_j*4 );*/
      return BAD;
    }
    PW_l  = in_data[PW_Start_Address+0x43e +PW_j*4]&0xf0;
    PW_m  = in_data[PW_Start_Address+0x43d +PW_j*4];
    if ( (PW_l != 0) || (PW_m > 0x1f))
    {
      /*printf ( "#5,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+0x43c +PW_j*4, PW_m );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_Polka ( void )
{
  /* PW_WholeSampleSize is the whole sample size */
  /* PW_k is the highest pattern number */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 0x43c;

  CONVERT = GOOD;
  Save_Rip ( "Polka Packed music", PolkaPacker );
  
  if ( Save_Status == GOOD )
    PW_i += 0x43C;  /* put back pointer after header*/
}



/*
 *   Polka.c   2003 (c) Asle
 *
*/

void Depack_Polka ( void )
{
  Uchar poss[37][2];
  Uchar c1=0x00,c2=0x00;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j;
  long Where = PW_Start_Address;
  FILE *out;
  unsigned char Whatever[4];

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* takes care of header */
  fwrite ( &in_data[Where], 20, 1, out );
  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( &in_data[Where+20+i*30], 18, 1, out );
    c1=0x00;
    fwrite ( &c1, 1, 1, out );fwrite ( &c1, 1, 1, out );
    fwrite ( &c1, 1, 1, out );fwrite ( &c1, 1, 1, out );
    fwrite ( &in_data[Where+42+i*30], 8, 1, out );
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write size of pattern list+ntk byte + pattern list */
  fwrite ( &in_data[Where+0x3b6] , 130 , 1 , out );

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
    if ( in_data[Where+i+0x3b8] > Max )
      Max = in_data[Where+i+0x3b8];
  }
  Max += 1;
  /*printf ( "\nNumber of pattern : %ld\n" , j );*/

  /* pattern data */
  Where = PW_Start_Address + 0x43c;
  for ( i=0 ; i<Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Whatever[0] = in_data[Where+1] & 0xf0;
      Whatever[2] = (in_data[Where+1] & 0x0f)<<4;
      Whatever[2] |= in_data[Where+2];
      Whatever[3] = in_data[Where+3];
      Whatever[0] |= poss[(in_data[Where])/2][0];
      Whatever[1] = poss[(in_data[Where]/2)][1];
      fwrite ( Whatever , 4 , 1 , out );
      Where += 4;
    }
  }

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "   Polka Packer   " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
