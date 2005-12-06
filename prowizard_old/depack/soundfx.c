/*
 *   SoundFX.c   1999 (c) Sylvain "Asle" Chipaux
 *
 * Depacks musics in the SoundFX format and saves in ptk.
 *
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Depack_SoundFX13 ( void )
{
  Uchar *Whatever;
  Uchar c0=0x00,c1=0x00,c2=0x00,c3=0x00;
  Uchar Max=0x00;
  Uchar PatPos;
  long WholeSampleSize=0;
  long i=0,j=0;
  FILE *in,*out;

  if ( Save_Status == BAD )
    return;

  in = fopen ( (char *)OutName_final , "r+b" ); /* +b is safe bcoz OutName's just been saved */
  if (!in)
      return;
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* title */
  Whatever = (Uchar *) malloc ( 20 );
  BZERO ( Whatever , 20 );
  fwrite ( Whatever , 20 , 1 , out );
  free ( Whatever );

  /* read and write whole header */
  for ( i=0 ; i<15 ; i++ )
  {
    fseek ( in , 0x50 + i*30 , 0 );
    /* write name */
    for ( j=0 ; j<22 ; j++ )
    {
      fread ( &c1 , 1 , 1 , in );
      fwrite ( &c1 , 1 , 1 , out );
    }
    /* size */
    fseek ( in , i*4 + 1 , 0 );
    fread ( &c0 , 1 , 1 , in );
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    c2 /= 2;
    c3 = c1/2;
    if ( (c3*2) != c1 )
      c2 += 0x80;
    if (c0 != 0x00)
      c3 += 0x80;
    fseek ( in , 0x50 + i*30 + 24 , 0 );
    fwrite ( &c3 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    WholeSampleSize += (((c3*256)+c2)*2);
    /* finetune */
    fread ( &c1 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    /* volume */
    fread ( &c1 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    /* loop start */
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    c2 /= 2;
    c3 = c1/2;
    if ( (c3*2) != c1 )
      c2 += 0x80;
    fwrite ( &c3 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    /* loop size */
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
  }
  free ( Whatever );
  Whatever = (Uchar *) malloc ( 30 );
  BZERO ( Whatever , 30 );
  Whatever[29] = 0x01;
  for ( i=0 ; i<16 ; i++ )
    fwrite ( Whatever , 30 , 1 , out );
  free ( Whatever );

  /* pattern list size */
  fread ( &PatPos , 1 , 1 , in );
  fwrite ( &PatPos , 1 , 1 , out );

  /* ntk byte */
  fseek ( in , 1 , 1 );
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* read and write pattern list */
  Max = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    fread ( &c1 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    if ( c1 > Max )
      Max = c1;
  }
  c1 = 0x00;
  while ( i != 128 )
  {
    fwrite ( &c1 , 1 , 1 , out );
    i+=1;
  }

  /* write ID */
  c1 = 'M';
  c2 = '.';
  c3 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );


  /* pattern data */
  fseek ( in , 0x294 , 0 );
  Whatever = (Uchar *) malloc ( 1024 );
  for ( i=0 ; i<=Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    fread ( Whatever , 1024 , 1 , in );
    for ( j=0 ; j<256 ; j++ )
    {
      if ( Whatever[(j*4)] == 0xff )
      {
        if ( Whatever[(j*4)+1] != 0xfe )
          printf ( "Volume unknown : (at:%ld) (fx:%x,%x,%x,%x)\n" , ftell (in)
                    , Whatever[(j*4)]
                    , Whatever[(j*4)+1]
                    , Whatever[(j*4)+2]
                    , Whatever[(j*4)+3] );
        Whatever[(j*4)]   = 0x00;
        Whatever[(j*4)+1] = 0x00;
        Whatever[(j*4)+2] = 0x0C;
        Whatever[(j*4)+3] = 0x00;
        continue;
      }
      switch ( Whatever[(j*4)+2]&0x0f )
      {
        case 1: /* arpeggio */
          Whatever[(j*4)+2] &= 0xF0;
          break;
        case 7: /* slide up */
        case 8: /* slide down */
          Whatever[(j*4)+2] -= 0x06;
          break;
        case 3: /* empty ... same as followings ... but far too much to "printf" it */
        case 6: /* and Noiseconverter puts 00 instead ... */
          Whatever[(j*4)+2] &= 0xF0;
          Whatever[(j*4)+3] = 0x00;
          break;
        case 2:
        case 4:
        case 5:
        case 9:
        case 0x0a:
        case 0x0b:
        case 0x0c:
        case 0x0d:
        case 0x0e:
        case 0x0f:
          printf ( "unsupported effect : (at:%ld) (fx:%d)\n" , ftell (in) , Whatever[(j*4)+2]&0x0f );
          Whatever[(j*4)+2] &= 0xF0;
          Whatever[(j*4)+3] = 0x00;
          break;
        default:
          break;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    fflush ( stdout );
  }
  free ( Whatever );
  fflush ( stdout );


  /* sample data */
  Whatever = (Uchar *) malloc ( WholeSampleSize );
  BZERO ( Whatever , WholeSampleSize );
  fread ( Whatever , WholeSampleSize , 1 , in );
  fwrite ( Whatever , WholeSampleSize , 1 , out );
  free ( Whatever );
  fflush ( stdout );


  /* crap */
  Crap ( "     Sound FX     " , BAD , BAD , out );

  fflush ( in );
  fflush ( out );
  fclose ( in );
  fclose ( out );

  printf ( "done\n"
           "  WARNING: This is only an under development converter !\n"
           "           output could sound strange...\n" );
  return; /* useless ... but */

}
