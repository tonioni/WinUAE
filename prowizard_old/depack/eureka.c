/*
 *   EurekaPacker.c   1997 (c) Asle / ReDoX
 *
 * Converts MODs packed with Eureka packer back to ptk
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

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
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

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

