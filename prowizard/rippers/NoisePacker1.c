/* testNoisepacker1() */
/* Rip_Noisepacker1() */
/* Depack_Noisepacker1() */

#include "globals.h"
#include "extern.h"


short testNoisepacker1 ( void )
{
  if ( PW_i < 15 )
  {
    return BAD;
  }
  PW_Start_Address = PW_i-15;

  /* size of the pattern table */
  PW_j = (in_data[PW_Start_Address+2]*256)+in_data[PW_Start_Address+3];
  if ( (((PW_j/2)*2) != PW_j) || (PW_j == 0) )
  {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the size of the pattern list (*2) */

  /* test nbr of samples */
  if ( (in_data[PW_Start_Address+1]&0x0f) != 0x0C )
  {
/*printf ( "#3,0 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_l = ((in_data[PW_Start_Address]<<4)&0xf0)|((in_data[PW_Start_Address+1]>>4)&0x0f);
  if ( (PW_l > 0x1F) || (PW_l == 0) || ((PW_Start_Address+PW_j+8+PW_l*8)>PW_in_size))
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l is the number of samples */

  /* test volumes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+15+PW_k*16] > 0x40 )
    {
/*printf ( "#3,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize=0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k*16+12]*256)+in_data[PW_Start_Address+PW_k*16+13];
    PW_m = (in_data[PW_Start_Address+PW_k*16+20]*256)+in_data[PW_Start_Address+PW_k*16+21];
    PW_n = (in_data[PW_Start_Address+PW_k*16+22]*256)+in_data[PW_Start_Address+PW_k*16+23];
    PW_o *= 2;
    PW_m *= 2;
    if ( (PW_o > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m + PW_n) > (PW_o+2) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_n != 0) && (PW_m == 0) )
    {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }


  /* small shit to gain some vars */
  PW_l *= 16;
  PW_l += 8;
  PW_l += 4;
  /* PW_l is now the size of the header 'til the end of sample descriptions */
  if (PW_l+PW_Start_Address > PW_in_size )
  {
    return BAD;
  }


  /* test pattern table */
  PW_n=0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k += 2 )
  {
    PW_m = ((in_data[PW_Start_Address+PW_l+PW_k]*256)+in_data[PW_Start_Address+PW_l+PW_k+1]);
    if ( ((PW_m/8)*8) != PW_m )
    {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_m > PW_n )
      PW_n = PW_m;
  }
  PW_l += PW_j;
  PW_l += PW_n;
  PW_l += 8; /*paske on a que l'address du dernier pattern .. */
  /* PW_l is now the size of the header 'til the end of the track list */
  /* PW_j is now available for use :) */

  /* test track data size */
  PW_k = (in_data[PW_Start_Address+6]*256)+in_data[PW_Start_Address+7];
  if ( (PW_k < 192) || (((PW_k/192)*192) != PW_k) )
  {
    /*printf ( "#7 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test notes */
  for ( PW_m=0 ; PW_m < PW_k ; PW_m+=3 )
  {
    if ( PW_Start_Address + PW_l + PW_m > PW_in_size )
    {
/*printf ( "#8,0 Start:%ld\n", PW_Start_Address );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_l+PW_m] > 0x49 )
    {
      /*printf ( "#8 Start:%ld (at %x)(PW_k:%x)(PW_l:%x)(PW_m:%x)\n" , PW_Start_Address,PW_Start_Address+PW_l+PW_m,PW_k,PW_l,PW_m );*/
      return BAD;
    }
  }

  /* PW_WholeSampleSize is the size of the sample data */
  /* PW_l is the size of the header 'til the track datas */
  /* PW_k is the size of the track datas */
  return GOOD;
}



void Rip_Noisepacker1 ( void )
{
  OutputSize = PW_k + PW_WholeSampleSize + PW_l;
  /*  printf ( "\b\b\b\b\b\b\b\bNoisePacker v1 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "NoisePacker v1 module", Noisepacker1 );
  
  if ( Save_Status == GOOD )
    PW_i += 16;  /* 15 should do but call it "just to be sure" :) */
}


/*
 *   NoisePacker_v1.c   1997 (c) Asle / ReDoX
 *
 * Converts NoisePacked MODs back to ptk
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up and Binary smaller.
 * Update:30/11/99
 *    - removed fopen() and attached funcs.
 * update: 30/08/10
 *    - Whatever wasn't cleaned up and was harmful when patternlist size was 1
*/
void Depack_Noisepacker1 ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar Nbr_Pos;
  Uchar poss[37][2];
  Uchar Pat_Max=0x00;
  long Max_Add=0;
  long WholeSampleSize=0;
  long TrackDataSize;
  long Track_Addresses[128][4];
  long Unknown1;
  long i=0,j=0,k;
  long Track_Data_Start_Address;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  BZERO ( Track_Addresses , 128*4*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read number of sample */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  Whatever[128] = ((in_data[Where]<<4)&0xf0) | ((in_data[Where+1]>>4)&0x0f);
  Where += 3;
  /*printf ( "Number of sample : %d (%x)\n" , Whatever[128] , Whatever[128] );*/

  /* write title */
  fwrite ( Whatever , 20 , 1 , out );

  /* read size of pattern list */
  Nbr_Pos = in_data[Where++]/2;
  /*printf ( "Size of pattern list : %d\n" , Nbr_Pos );*/

  /* read 2 unknown bytes which size seem to be of some use ... */
  Unknown1 = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;

  /* read track data size */
  TrackDataSize = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  /*printf ( "TrackDataSize : %ld\n" , TrackDataSize );*/

  /* read sample descriptions */
  for ( i=0 ; i<Whatever[128] ; i++ )
  {
    /* sample name */
    fwrite ( Whatever , 22 , 1 , out );
    WholeSampleSize += (((in_data[Where+4]*256)+in_data[Where+5])*2);
    /* size,fine,vol */
    fwrite ( &in_data[Where+4] , 4 , 1 , out );

    /* read loop start -- coz it's NOT /2 !*/
    Whatever[32] = in_data[Where+14];
    Whatever[33] = in_data[Where+15];
    /* write loop start */
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
    fwrite ( &Whatever[32] , 2 , 1 , out );
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
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );
  BZERO ( Whatever , 1024 );

  /* bypass 2 bytes ... seems always the same as in $02 */
  /* & bypass 2 other bytes which meaning is beside me */
  Where += 4;

  /* read pattern table */
  Pat_Max = 0x00;
  for ( i=0 ; i<Nbr_Pos ; i++ )
  {
    Whatever[i] = ((in_data[Where+(i*2)]*256)+in_data[Where+(i*2)+1])/8;
    if ( Whatever[i] > Pat_Max )
      Pat_Max = Whatever[i];
  }
  Pat_Max += 1;
  Where += Nbr_Pos*2;
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
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    Track_Addresses[i][0] = (in_data[Where+(i*8)]*256)+in_data[Where+(i*8)+1];
    if ( Track_Addresses[i][0] > Max_Add )
      Max_Add = Track_Addresses[i][0];
    Track_Addresses[i][1] = (in_data[Where+(i*8)+2]*256)+in_data[Where+(i*8)+3];
    if ( Track_Addresses[i][1] > Max_Add )
      Max_Add = Track_Addresses[i][1];
    Track_Addresses[i][2] = (in_data[Where+(i*8)+4]*256)+in_data[Where+(i*8)+5];
    if ( Track_Addresses[i][2] > Max_Add )
      Max_Add = Track_Addresses[i][2];
    Track_Addresses[i][3] = (in_data[Where+(i*8)+6]*256)+in_data[Where+(i*8)+7];
    if ( Track_Addresses[i][3] > Max_Add )
      Max_Add = Track_Addresses[i][3];
  }
  Track_Data_Start_Address = (Where + (Pat_Max*8));

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
        if ( (c2&0x0f) == 0x0B )
        {
          c3 += 0x04;
          c3 /= 2;
        }
        Whatever[k*16+j*4+2] = c2;
        Whatever[k*16+j*4+3] = c3;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );

  /* sample data */
  Where = Max_Add+192+Track_Data_Start_Address;
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  Crap ( "  NoisePacker v1  " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
