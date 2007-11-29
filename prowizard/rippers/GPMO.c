/* testGPMO() */
/* Rip_GPMO() */
/* Depack_GPMO() */


#include "globals.h"
#include "extern.h"


short testGPMO ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    /*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps (PW_j,PW_m,PW_n,in_data[PW_Start_Address+45+PW_k*30]/2,in_data[PW_Start_Address+44+PW_k*30]) == BAD )
    {
      /*      printf ( "#2 (Start:%ld)(siz:%ld)(lstart:%ld)(lsiz:%ld)\n" , PW_Start_Address,PW_j,PW_m,PW_n );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+950];
  if ( (PW_l>127) || (PW_l==0) )
  {
    /*    printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+952+PW_j];
    if ( in_data[PW_Start_Address+952+PW_j] > 127 )
    {
      /*      printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* found some obscure ptk :( */
  while ( PW_j < 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > 0x7f )
    {
      /*      printf ( "#4,2 (Start:%ld) (PW_j:%ld) (at:%ld)\n" , PW_Start_Address,PW_j ,PW_Start_Address+952+PW_j );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;

  /*  if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+1084+PW_Start_Address) > PW_in_size )
  {
    /*    printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  for ( PW_j=0 ; PW_j<(PW_k*256) ; PW_j++ )
  {
    /* sample > 1f   or   pitch > 358 ? */
    if ( in_data[PW_Start_Address+1084+PW_j*4] > 0x13 )
    {
      /*      printf ( "#5.1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_l = ((in_data[PW_Start_Address+1084+PW_j*4]&0x0f)*256)+in_data[PW_Start_Address+1085+PW_j*4];
    if ( (PW_l>0) && (PW_l<0x1C) )
    {
      /*      printf ( "#5,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}


void Rip_GPMO ( void )
{
  /* PW_k is still the nbr of pattern */

  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+in_data[PW_Start_Address+43+PW_j*30])*2);

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  CONVERT = BAD;
  Save_Rip ( "GPMO (Crunch Player) module", GPMO );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1081);  /* 1080 should do but call it "just to be sure" :) */
}



/*
 *   gpmo.c   2003 (c) Asle / ReDoX
 *
 * Converts GPMO MODs back to PTK
 *
*/

void Depack_GPMO ( void )
{
  Uchar *Whatever;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0;
  long Where=PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* get whole sample size and patch vols (/2)*/
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
    in_data[Where+45+i*30] = in_data[Where+45+i*30]/2;
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write whole header */
  fwrite ( &in_data[Where] , 1080 , 1 , out );

  Where += 952;

  /* write ID */
  Whatever = (Uchar *) malloc (4);
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );
  free ( Whatever );

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
  fwrite ( &in_data[Where], (Max+1)*1024, 1, out);
  Where += ((Max+1)*1024);

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "       GPMO       " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

