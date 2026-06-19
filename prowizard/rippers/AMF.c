/* (06 dec 2008)
 AMF "advanced module format" (Dual Module Player internal format)
 dirty AMF->MOD depack, when possible.
 (c) Sylvain "Asle" Chipaux
*/
/* testAMF() */
/* Rip_AMF() */
/* Depack_AMF() */


#include "globals.h"
#include "extern.h"


int16_t	 testAMF ( void )
{
  PW_Start_Address = PW_i;
  /* test 1 */
  if ( (PW_Start_Address+75)>=PW_in_size )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_j = in_data[PW_Start_Address+36]; /* nbr of samples */
  PW_k = in_data[PW_Start_Address+40]; /* nbr of voices */
  if ( (PW_j>0x1F) || (PW_k>0x04) ) /* yes, 4 voices only .. I _want_ PTK ..*/
  {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}


void Rip_AMF ( void )
{
  /* PW_j is still the number of samples */
  /* PW_k is still the number of voices */

/* TODO */

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
 *   AMF.c   2008 (c) Sylvain "Asle" Chipaux
 *
 * Converts MODs packed DMP replayer
*/

void Depack_AMF ( void )
{
  uint8_t *Whatever;
  uint8_t c1=0x00;
  uint8_t Pat_Max=0x00;
  int32_t	 Sample_Start_Address=0;
  int32_t	 WholeSampleSize=0;
  int32_t	 Track_Address[128][4];
  int32_t	 i=0,j=0,k;
  int32_t	 Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
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
  Whatever = (uint8_t *) malloc (1024);
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

