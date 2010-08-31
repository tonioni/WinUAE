/* testEUREKA() */
/* Rip_EUREKA() */
/* Depack_EUREKA() */


#include "globals.h"
#include "extern.h"


/* (27 dec 2001)
 *   added some checks to prevent readings outside of input file (in test 1)
 * (May 2002)
 *   added test_smps()
 * (30/08/10)
 *   changed #4.3 as the "remaining" patternlist isn't always 0x00
*/
short testEUREKA ( void )
{
  /* test 1 */
  if ( (PW_i < 45) || ((PW_Start_Address+950)>=PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-45;
  PW_j = in_data[PW_Start_Address+950];
  if ( (PW_j==0) || (PW_j>127) )
  {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3  finetunes & volumes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+42+PW_k*30]*256) + in_data[PW_Start_Address+43+PW_k*30];
    PW_m = (in_data[PW_Start_Address+46+PW_k*30]*256) + in_data[PW_Start_Address+47+PW_k*30];
    PW_n = (in_data[PW_Start_Address+48+PW_k*30]*256) + in_data[PW_Start_Address+49+PW_k*30];
    PW_o *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( (PW_o > 0xffff) ||
         (PW_m > 0xffff) ||
         (PW_n > 0xffff) )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( test_smps ( PW_o+2, PW_m, PW_n, in_data[PW_Start_Address+45+PW_k*30], in_data[PW_Start_Address+44+PW_k*30] ) == BAD)
      return BAD;
    PW_WholeSampleSize += PW_o;
  }


  /* test 4 */
  PW_l = (in_data[PW_Start_Address+1080]*256*256*256)
    +(in_data[PW_Start_Address+1081]*256*256)
    +(in_data[PW_Start_Address+1082]*256)
    +in_data[PW_Start_Address+1083];
  if ( (PW_l+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_l < 1084 )
  {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_m=0;
  /* pattern list */
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    PW_n = in_data[PW_Start_Address+952+PW_k];
    if ( PW_n > PW_m )
      PW_m = PW_n;
    if ( PW_n > 127 )
    {
/*printf ( "#4,2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  PW_k += 2; /* to be sure .. */
  while ( PW_k != 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_k] > 0x64 )
    {
/*printf ( "#4,3 (Start:%ld)(PW_k:%ld)\n" , PW_Start_Address,PW_k );*/
      return BAD;
    }
    PW_k += 1;
  }
  PW_m += 1;
  /* PW_m is the highest pattern number */


  /* test #5 */
  /* PW_j is still the size if the pattern table */
  /* PW_l is still the address of the sample data */
  /* PW_m is the highest pattern number */
  PW_n=0;
  PW_j=999999l;
  for ( PW_k=0 ; PW_k<(PW_m*4) ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k*2+1084]*256)+in_data[PW_Start_Address+PW_k*2+1085];
    if ( (PW_o > PW_l) || (PW_o < 1084) )
    {
/*printf ( "#5 (Start:%ld)(PW_k:%ld)(PW_j:%ld)(PW_l:%ld)\n" , PW_Start_Address,PW_k,PW_j,PW_l );*/
      return BAD;
    }
    if ( PW_o > PW_n )
      PW_n = PW_o;
    if ( PW_o < PW_j )
      PW_j = PW_o;
  }
  /* PW_o is the highest track address */
  /* PW_j is the lowest track address */

  /* test track datas */
  /* last track wont be tested ... */
  for ( PW_k=PW_j ; PW_k<PW_o ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0xC0 )
      continue;
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x80 )
    {
      PW_k += 2;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x40 )
    {
      if ( ((in_data[PW_Start_Address+PW_k]&0x3F) == 0x00) &&
	   (in_data[PW_Start_Address+PW_k+1] == 0x00) )
      {
/*printf ( "#6 Start:%ld (at:%x)\n" , PW_Start_Address , PW_k );*/
	return BAD;
      }
      PW_k += 1;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_k]&0xC0) == 0x00 )
    {
      if ( in_data[PW_Start_Address+PW_k] > 0x13 )
      {
/*printf ( "#6,1 Start:%ld (at:%x)\n" , PW_Start_Address , PW_k );*/
	return BAD;
      }
      PW_k += 3;
      continue;
    }
  }

  return GOOD;
}


void Rip_EUREKA ( void )
{
  /* PW_l is still the sample data address */

  /*PW_WholeSampleSize is already the whole sample size */
  /*for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+in_data[PW_Start_Address+43+PW_j*30])*2);*/

  OutputSize = PW_WholeSampleSize + PW_l;

  CONVERT = GOOD;
  Save_Rip ( "Eureka Packed module", Eureka_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 46);  /* 45 should do but call it "just to be sure" :) */
}


/*
 *   EurekaPacker.c   1997 (c) Asle / ReDoX
 *
 * Converts MODs packed with Eureka packer back to ptk
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 * 20051002 : testing fopen()
*/

void Depack_EUREKA ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00;
  Uchar Pat_Max=0x00;
  long Sample_Start_Address=0;
  long WholeSampleSize=0;
  long Track_Address[128][4];
  long i=0,j=0,k;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read header ... same as ptk */
  fwrite ( &in_data[Where] , 1080 , 1 , out );

  /* now, let's sort out that a bit :) */
  /* first, the whole sample size */
  for ( i=0 ; i<31 ; i++ )
    WholeSampleSize += (((in_data[Where+i*30+42]*256)+in_data[Where+i*30+43])*2);
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* next, the size of the pattern list */
  /*printf ( "Size of pattern list : %d\n" , in_data[Where+950] );*/

  /* now, the pattern list .. and the max */
  Pat_Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+952+i] > Pat_Max )
      Pat_Max = in_data[Where+952+i];
  }
  Pat_Max += 1;
  /*printf ( "Number of patterns : %d\n" , Pat_Max );*/

  /* write ptk's ID */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* read sample data address */
  Where = PW_Start_Address+1080;
  Sample_Start_Address = (in_data[Where]*256*256*256)+
                         (in_data[Where+1]*256*256)+
                         (in_data[Where+2]*256)+
                          in_data[Where+3];
  Where += 4;
  /*printf ( "Address of sample data : %ld\n" , Sample_Start_Address );*/

  /* read tracks addresses */
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      Track_Address[i][j] = (in_data[Where]*256)+in_data[Where+1];
      Where += 2;
    }
  }

  /* the track data now ... */
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      Where = PW_Start_Address + Track_Address[i][j];
      for ( k=0 ; k<64 ; k++ )
      {
        c1 = in_data[Where++];
        if ( ( c1 & 0xc0 ) == 0x00 )
        {
          Whatever[k*16+j*4]   = c1;
          Whatever[k*16+j*4+1] = in_data[Where++];
          Whatever[k*16+j*4+2] = in_data[Where++];
          Whatever[k*16+j*4+3] = in_data[Where++];
          continue;
        }
        if ( ( c1 & 0xc0 ) == 0xc0 )
        {
          k += (c1&0x3f);
          continue;
        }
        if ( ( c1 & 0xc0 ) == 0x40 )
        {
          Whatever[k*16+j*4+2] = c1&0x0f;
          Whatever[k*16+j*4+3] = in_data[Where++];
          continue;
        }
        if ( ( c1 & 0xc0 ) == 0x80 )
        {
          Whatever[k*16+j*4] = in_data[Where++];
          Whatever[k*16+j*4+1] = in_data[Where++];
          Whatever[k*16+j*4+2] = (c1<<4)&0xf0;
          continue;
        }
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
  }
  free ( Whatever );

  /* go to sample data addy */
  Where = PW_Start_Address + Sample_Start_Address;

  /* read sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap .. */
  Crap ( "  EUREKA Packer   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

