/*
 *   NoisePacker_v3.c   1998 (c) Asle / ReDoX
 *
 * Converts NoisePacked MODs back to ptk
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up and Binary smaller.
 * update : 01/12/99
 *   - removed fopen() and attached funcs.
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_Noisepacker3 ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar Nbr_Pos;
  Uchar Nbr_Smp;
  Uchar poss[36][2];
  Uchar Pat_Max=0x00;
  long Where=PW_Start_Address;
  long WholeSampleSize=0;
  long TrackDataSize;
  long Track_Addresses[128][4];
  long Unknown1;
  long i=0,j=0,k;
  long Track_Data_Start_Address;
  long SampleDataAddress=0;
  FILE *out;

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  BZERO ( Track_Addresses , 128*4*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* read number of sample */
  Nbr_Smp = ((in_data[Where]<<4)&0xf0) | ((in_data[Where+1]>>4)&0x0f);
  /*printf ( "\nNumber of sample : %d (%x)\n" , Nbr_Smp , Nbr_Smp );*/

  /* write title */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read size of pattern list */
  Nbr_Pos = in_data[Where+3]/2;
  /*printf ( "Size of pattern list : %d\n" , Nbr_Pos );*/

  /* read 2 unknown bytes which size seem to be of some use ... */
  Unknown1 = (in_data[Where+4]*256)+in_data[Where+5];

  /* read track data size */
  TrackDataSize = (in_data[Where+6]*256)+in_data[Where+7];
  /*printf ( "TrackDataSize : %ld\n" , TrackDataSize );*/

  /* read sample descriptions */
  Where += 8;
  for ( i=0 ; i<Nbr_Smp ; i++ )
  {
    /* sample name */
    fwrite ( Whatever , 22 , 1 , out );
    /* size */
    fwrite ( &in_data[Where+6] , 2 , 1 , out );
    WholeSampleSize += (((in_data[Where+6]*256)+in_data[Where+7])*2);
    /* write finetune,vol */
    fwrite ( &in_data[Where] , 2 , 1 , out );
    /* write loop start */
    fwrite ( &in_data[Where+14] , 2 , 1 , out );
    /* write loop size */
    fwrite ( &in_data[Where+12] , 2 , 1 , out );
    Where += 16;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* fill up to 31 samples */
  Whatever[29] = 0x01;
  while ( i != 31 )
  {
    fwrite ( Whatever , 30 , 1 , out );
    i += 1;
  }

  /* write size of pattern list */
  fwrite ( &Nbr_Pos , 1 , 1 , out );

  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* bypass 2 bytes ... seems always the same as in $02 */
  /* & bypass 2 other bytes which meaning is beside me */
  Where += 4;

  /* read pattern table */
  Pat_Max = 0x00;
  for ( i=0 ; i<Nbr_Pos ; i++ )
  {
    Whatever[i] = ((in_data[Where+(i*2)]*256)+in_data[Where+(i*2)+1])/8;
    /*printf ( "%d," , Whatever[i] );*/
    if ( Whatever[i] > Pat_Max )
      Pat_Max = Whatever[i];
  }
  Where += Nbr_Pos*2;
  Pat_Max += 1;
  /*printf ( "Number of pattern : %d\n" , Pat_Max );*/

  /* write pattern table */
  fwrite ( Whatever , 128 , 1 , out );

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* read tracks addresses per pattern */
  /*printf ( "\nWhere : %ld\n" , Where );*/
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    Track_Addresses[i][0] = (in_data[Where+(i*8)]*256)+in_data[Where+(i*8)+1];
    Track_Addresses[i][1] = (in_data[Where+(i*8)+2]*256)+in_data[Where+(i*8)+3];
    Track_Addresses[i][2] = (in_data[Where+(i*8)+4]*256)+in_data[Where+(i*8)+5];
    Track_Addresses[i][3] = (in_data[Where+(i*8)+6]*256)+in_data[Where+(i*8)+7];
  }
  Track_Data_Start_Address = (Where + (Pat_Max*8));
  /*printf ( "Track_Data_Start_Address : %ld\n" , Track_Data_Start_Address );*/

  /* the track data now ... */
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      Where = Track_Data_Start_Address + Track_Addresses[i][3-j];
      for ( k=0 ; k<64 ; k++ )
      {
        c1 = in_data[Where];
        Where += 1;
        if ( c1 >= 0x80 )
        {
          k += ((0x100-c1)-1);
          continue;
        }
        c2 = in_data[Where];
        Where += 1;
        c3 = in_data[Where];
        Where += 1;

        Whatever[k*16+j*4]   = (c1<<4)&0x10;
        c4 = (c1 & 0xFE)/2;
        Whatever[k*16+j*4] |= poss[c4][0];
        Whatever[k*16+j*4+1] = poss[c4][1];
        if ( (c2&0x0f) == 0x08 )
          c2 &= 0xf0;
        if ( (c2&0x0f) == 0x07 )
        {
          c2 = (c2&0xf0)+0x0A;
          if ( c3 > 0x80 )
            c3 = 0x100-c3;
          else
            c3 = (c3<<4)&0xf0;
        }
        if ( (c2&0x0f) == 0x06 )
        {
          if ( c3 > 0x80 )
            c3 = 0x100-c3;
          else
            c3 = (c3<<4)&0xf0;
        }
        if ( (c2&0x0f) == 0x05 )
        {
          if ( c3 > 0x80 )
            c3 = 0x100-c3;
          else
            c3 = (c3<<4)&0xf0;
        }
        if ( (c2&0x0f) == 0x0E )
        {
          c3 = 0x01;
        }
        if ( (c2&0x0f) == 0x0B )
        {
          c3 += 0x04;
          c3 /= 2;
        }
        Whatever[k*16+j*4+2] = c2;
        Whatever[k*16+j*4+3] = c3;
        if ( (c2&0x0f) == 0x0D )
          k = 100; /* to leave the loop */
      }
      if ( Where > SampleDataAddress )
        SampleDataAddress = Where;
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );

  /* sample data */
  if ( (((SampleDataAddress-PW_Start_Address)/2)*2) != SampleDataAddress )
    SampleDataAddress += 1;
  Where = SampleDataAddress;
  /*printf ( "Starting address of sample data : %x\n" , ftell ( in ) );*/
  fwrite ( &in_data[SampleDataAddress] , WholeSampleSize , 1 , out );

  Crap ( "  NoisePacker v3  " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
