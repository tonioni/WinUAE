/* testGMC() */
/* Rip_GMC() */
/* Depack_GMC() */

#include "globals.h"
#include "extern.h"


short testGMC ( void )
{
  /* test #1 */
  if ( (PW_i<7) || ((PW_Start_Address+444)>PW_in_size) )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-7;

  /* samples descriptions */
  PW_WholeSampleSize=0;
  PW_j=0;
  for ( PW_k = 0 ; PW_k < 15 ; PW_k ++ )
  {
    PW_o = (in_data[PW_Start_Address+16*PW_k+4]*256)+in_data[PW_Start_Address+16*PW_k+5];
    PW_n = (in_data[PW_Start_Address+16*PW_k+12]*256)+in_data[PW_Start_Address+16*PW_k+13];
    PW_o *= 2;
    /* volumes */
    if ( in_data[PW_Start_Address + 7 + (16*PW_k)] > 0x40 )
    {
/*printf ( "#2\n" );*/
      return BAD;
    }
    /* size */
    if ( PW_o > 0xFFFF )
    {
/*printf ( "#2,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_n > PW_o )
    {
/*printf ( "#2,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
    if ( PW_o != 0 )
      PW_j = PW_k+1;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#2,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the highest not null sample */

  /* pattern table size */
  if ( ( in_data[PW_Start_Address+243] > 0x64 ) ||
       ( in_data[PW_Start_Address+243] == 0x00 ) )
  {
    return BAD;
  }

  /* pattern order table */
  PW_l=0;
  for ( PW_n=0 ; PW_n<100 ; PW_n++ )
  {
    PW_k = ((in_data[PW_Start_Address+244+PW_n*2]*256)+
	    in_data[PW_Start_Address+245+PW_n*2]);
    if ( ((PW_k/1024)*1024) != PW_k )
    {
/*printf ( "#4 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
      return BAD;
    }
    PW_l = ((PW_k/1024)>PW_l) ? PW_k/1024 : PW_l;
  }
  PW_l += 1;
  /* PW_l is the number of pattern */
  if ( (PW_l == 1) || (PW_l >0x64) )
  {
    return BAD;
  }

  /* test pattern data */
  PW_o = in_data[PW_Start_Address+243];
  PW_m = 0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    for ( PW_n=0 ; PW_n<256 ; PW_n++ )
    {
      if ( ( in_data[PW_Start_Address+444+PW_k*1024+PW_n*4] > 0x03 ) ||
	   ( (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) >= 0x90 ))
      {
/*printf ( "#5,0 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) > PW_j )
      {
/*printf ( "#5,1 Start:%ld (PW_j:%ld) (where:%ld) (value:%x)\n"
         , PW_Start_Address , PW_j , PW_Start_Address+444+PW_k*1024+PW_n*4+2
         , ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 3) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x40) )
      {
/*printf ( "#5,2 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 4) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x63) )
      {
/*printf ( "#5,3 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 5) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > PW_o+1) )
      {
/*printf ( "#5,4 Start:%ld (effect:5)(PW_o:%ld)(4th note byte:%x)\n" , PW_Start_Address , PW_j , in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3]);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 6) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,5 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 7) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,6 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4]&0x0f) > 0x00) || (in_data[PW_Start_Address+445+PW_k*1024+PW_n*4] > 0x00) )
	PW_m = 1;
    }
  }
  if ( PW_m == 0 )
  {
    /* only empty notes */
    return BAD;
  }
  /* PW_WholeSampleSize is the whole sample size */

  return GOOD;
}


void Rip_GMC ( void )
{
  /* PW_l is still the number of pattern to play */
  /* PW_WholeSampleSize is already the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 444;

  CONVERT = GOOD;
  Save_Rip ( "Game Music Creator module", GMC );
  
  if ( Save_Status == GOOD )
    PW_i += 444; /* after header */
}

/*
 *   Game_Music_Creator.c   1997 (c) Sylvain "Asle" Chipaux
 *
 * Depacks musics in the Game Music Creator format and saves in ptk.
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/


void Depack_GMC ( void )
{
  Uchar *Whatever;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  if (!out)
    return;

  /* title */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write whole header */
  /*printf ( "Converting sample headers ... " );*/
  for ( i=0 ; i<15 ; i++ )
  {
    /* write name */
    fwrite ( Whatever , 22 , 1 , out );

    /* size */
    fwrite ( &in_data[Where+4] , 2 , 1 , out );
    WholeSampleSize += (((in_data[Where+4]*256)+in_data[Where+5])*2);

    /* finetune */
    fwrite ( Whatever , 1 , 1 , out );

    /* volume */
    fwrite ( &in_data[Where+7] , 1 , 1 , out );

    /* loop size */
    Whatever[32] = in_data[Where+12];
    Whatever[33] = in_data[Where+13];

    /* loop start */
    Whatever[34] = in_data[Where+14];
    Whatever[35] = in_data[Where+15];
    Whatever[35] /= 2;
    if ( (Whatever[34]/2)*2 != Whatever[34] )
    {
      if ( Whatever[35] < 0x80 )
        Whatever[35] += 0x80;
      else
      {
        Whatever[35] -= 0x80;
        Whatever[34] += 0x01;
      }
    }
    Whatever[34] /= 2;
    fwrite ( &Whatever[34] , 1 , 1 , out );
    fwrite ( &Whatever[35] , 1 , 1 , out );
    Whatever[33] /= 2;
    if ( (Whatever[32]/2)*2 != Whatever[32] )
    {
      if ( Whatever[33] < 0x80 )
        Whatever[33] += 0x80;
      else
      {
        Whatever[33] -= 0x80;
        Whatever[32] += 0x01;
      }
    }
    Whatever[32] /= 2;
    if ( (Whatever[32]==0x00) && (Whatever[33]==0x00) )
      Whatever[33] = 0x01;
    fwrite ( &Whatever[32] , 1 , 1 , out );
    fwrite ( &Whatever[33] , 1 , 1 , out );

    Where += 16;
  }
  Whatever[129] = 0x01;
  for ( i=0 ; i<16 ; i++ )
    fwrite ( &Whatever[100] , 30 , 1 , out );
  /*printf ( "ok\n" );*/

  /* pattern list size */
  Where = PW_Start_Address + 0xF3;
  fwrite ( &in_data[Where++] , 1 , 1 , out );

  /* ntk byte */
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );

  /* read and write size of pattern list */
  /*printf ( "Creating the pattern table ... " );*/
  BZERO (Whatever , 1024);
  for ( i=0 ; i<100 ; i++ )
  {
    Whatever[i] = ((in_data[Where]*256)+in_data[Where+1])/1024;
    Where += 2;
  }
  fwrite ( Whatever , 128 , 1 , out );

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( Whatever[i] > Max )
      Max = Whatever[i];
  }
  /*printf ( "ok\n" );*/


  /* write ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* pattern data */
  /*printf ( "Converting pattern datas " );*/
  Where = PW_Start_Address + 444;
  for ( i=0 ; i<=Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<1024 ; j++ ) Whatever[j] = in_data[Where++];
    for ( j=0 ; j<256 ; j++ )
    {
      switch ( Whatever[(j*4)+2]&0x0f )
      {
        case 3: /* replace by C */
          Whatever[(j*4)+2] += 0x09;
          break;
        case 4: /* replace by D */
          Whatever[(j*4)+2] += 0x09;
          break;
        case 5: /* replace by B */
          Whatever[(j*4)+2] += 0x06;
          break;
        case 6: /* replace by E0 */
          Whatever[(j*4)+2] += 0x08;
          break;
        case 7: /* replace by E0 */
          Whatever[(j*4)+2] += 0x07;
          break;
        case 8: /* replace by F */
          Whatever[(j*4)+2] += 0x07;
          break;
        default:
          break;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "." );*/
    /*fflush ( stdout );*/
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/
  /*fflush ( stdout );*/

  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );
  /*printf ( "ok\n" );*/
  /*fflush ( stdout );*/

  /* crap */
  Crap ( "Game Music Creator" , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
