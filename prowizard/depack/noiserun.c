/*
 *   NoiseRunner.c   1997 (c) Asle / ReDoX
 *
 * NoiseRunner to Protracker.
 *
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up, Clean-up and Binary smaller.
 * update: 01/12/99
 *   - removed fopen() and attached funcs
 * Another Update : 26 nov 2003
 *   - used htonl() so that use of addy is now portable on 68k archs
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

#define PAT_DATA_ADDRESS 0x43C

void Depack_Noiserunner ( void )
{
  Uchar poss[37][2];
  Uchar Max=0x00;
  Uchar Note,Smp,Fx,FxVal;
  Uchar *Whatever;
  long Where=PW_Start_Address;
  long i=0,j=0,l=0,k;
  long WholeSampleSize=0;
  FILE *out;

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  /* 31 samples */
  /*printf ( "Converting sample headers ... " );*/
  for ( i=0 ; i<31 ; i++ )
  {
    /* sample name */
    fwrite ( Whatever , 22 , 1 , out );

    /* read sample address */
    j = ((in_data[Where+2]*256*256*256)+
         (in_data[Where+3]*256*256)+
         (in_data[Where+4]*256)+
          in_data[Where+5]);

    /* read and write sample size */
    fwrite ( &in_data[Where+6] , 2 , 1 , out );
    WholeSampleSize += (((in_data[Where+6]*256)+in_data[Where+7])*2);

    /* read loop start address */
    l = ((in_data[Where+8]*256*256*256)+
         (in_data[Where+9]*256*256)+
         (in_data[Where+10]*256)+
          in_data[Where+11]);

    /* calculate loop start value */
    j = l-j;

    /* read finetune ?!? */
    Whatever[32] = in_data[Where+14];
    Whatever[33] = in_data[Where+15];
    if ( Whatever[32] > 0xf0 )
    {
      if ( (Whatever[32] == 0xFB) && (Whatever[33] == 0xC8) )
	Whatever[32] = 0x0f;
      if ( (Whatever[32] == 0xFC) && (Whatever[33] == 0x10) )
	Whatever[32] = 0x0E;
      if ( (Whatever[32] == 0xFC) && (Whatever[33] == 0x58) )
	Whatever[32] = 0x0D;
      if ( (Whatever[32] == 0xFC) && (Whatever[33] == 0xA0) )
	Whatever[32] = 0x0C;
      if ( (Whatever[32] == 0xFC) && (Whatever[33] == 0xE8) )
	Whatever[32] = 0x0B;
      if ( (Whatever[32] == 0xFD) && (Whatever[33] == 0x30) )
	Whatever[32] = 0x0A;
      if ( (Whatever[32] == 0xFD) && (Whatever[33] == 0x78) )
	Whatever[32] = 0x09;
      if ( (Whatever[32] == 0xFD) && (Whatever[33] == 0xC0) )
	Whatever[32] = 0x08;
      if ( (Whatever[32] == 0xFE) && (Whatever[33] == 0x08) )
	Whatever[32] = 0x07;
      if ( (Whatever[32] == 0xFE) && (Whatever[33] == 0x50) )
	Whatever[32] = 0x06;
      if ( (Whatever[32] == 0xFE) && (Whatever[33] == 0x98) )
	Whatever[32] = 0x05;
      if ( (Whatever[32] == 0xFE) && (Whatever[33] == 0xE0) )
	Whatever[32] = 0x04;
      if ( (Whatever[32] == 0xFF) && (Whatever[33] == 0x28) )
	Whatever[32] = 0x03;
      if ( (Whatever[32] == 0xFF) && (Whatever[33] == 0x70) )
	Whatever[32] = 0x02;
      if ( (Whatever[32] == 0xFF) && (Whatever[33] == 0xB8) )
	Whatever[32] = 0x01;
    }
    else
      Whatever[32] = 0x00;

    /* write fine */
    fwrite ( &Whatever[32] , 1 , 1 , out );

    /* write vol */
    fwrite ( &in_data[Where+1] , 1 , 1 , out );

    /* write loop start */
    /* use of htonl() suggested by Xigh !.*/
    j/=2;
    k = htonl(j);
    Whatever[32] = *((Uchar *)&k+2);
    Whatever[33] = *((Uchar *)&l+3);
    fwrite ( &Whatever[32] , 2 , 1 , out );

    /* write loop size */
    fwrite ( &in_data[Where+12] , 2 , 1 , out );
    Where += 16;
  }
  /*printf ( "ok\n" );*/
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* pattablesiz & Ntk Byte & pattern table */
  Where = PW_Start_Address + 950;
  fwrite ( &in_data[Where] , 130 , 1 , out );
  Where += 2;

  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where] > Max )
      Max = in_data[Where];
    Where += 1;
  }
  Max += 1;  /* starts at $00 */
  /*printf ( "number of pattern : %d\n" , Max );*/

  /* write Protracker's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  Where = PW_Start_Address + PAT_DATA_ADDRESS;
  for ( i=0 ; i<Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<256 ; j++ )
    {
      Smp  = (in_data[Where+j*4+3]>>3)&0x1f;
      Note = in_data[Where+j*4+2];
      Fx   = in_data[Where+j*4];
      FxVal= in_data[Where+j*4+1];
      switch ( Fx )
      {
        case 0x00:  /* tone portamento */
          Fx = 0x03;
          break;

        case 0x04:  /* slide up */
          Fx = 0x01;
          break;

        case 0x08:  /* slide down */
          Fx = 0x02;
          break;

        case 0x0C:  /* no Fx */
          Fx = 0x00;
          break;

        case 0x10:  /* set vibrato */
          Fx = 0x04;
          break;

        case 0x14:  /* portamento + volume slide */
          Fx = 0x05;
          break;

        case 0x18:  /* vibrato + volume slide */
          Fx = 0x06;
          break;

        case 0x20:  /* set panning ?!?!? not PTK ! Heh, Gryzor ... */
          Fx = 0x08;
          break;

        case 0x24:  /* sample offset */
          Fx = 0x09;
          break;

        case 0x28:  /* volume slide */
          Fx = 0x0A;
          break;

        case 0x30:  /* set volume */
          Fx = 0x0C;
          break;

        case 0x34:  /* pattern break */
          Fx = 0x0D;
          break;

        case 0x38:  /* extended command */
          Fx = 0x0E;
          break;

        case 0x3C:  /* set speed */
          Fx = 0x0F;
          break;

        default:
          /*printf ( "%x : at %x\n" , Fx , i*1024 + j*4 + 1084 );*/
          Fx = 0x00;
          break;
      }
      Whatever[j*4] = (Smp & 0xf0);
      Whatever[j*4] |= poss[(Note/2)][0];
      Whatever[j*4+1] = poss[(Note/2)][1];
      Whatever[j*4+2] = ((Smp<<4)&0xf0);
      Whatever[j*4+2] |= Fx;
      Whatever[j*4+3] = FxVal;
    }
    Where += 1024;
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "pattern %ld written\n" , i );*/
  }
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  Crap ( "   Noiserunner    " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

