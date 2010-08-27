/* testNoisepacker3() */
/* Rip_Noisepacker3() */
/* Depack_Noisepacker3() */

#include "globals.h"
#include "extern.h"

short testNoisepacker3 ( void )
{
  if ( PW_i < 9 )
  {
    return BAD;
  }
  PW_Start_Address = PW_i-9;

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
/*printf ( "#3,0 Start:%ld (nbr sample : %d)\n" , PW_Start_Address, in_data[PW_Start_Address+1]&0x0f);*/
    return BAD;
  }
  PW_l = ((in_data[PW_Start_Address]<<4)&0xf0)|((in_data[PW_Start_Address+1]>>4)&0x0f);
  if ( (PW_l > 0x1F) || (PW_l == 0) || ((PW_Start_Address+8+PW_j+PW_l*8)>PW_in_size))
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l is the number of samples */

  /* test volumes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+9+PW_k*16] > 0x40 )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize=0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_k*16+14]*256)+in_data[PW_Start_Address+PW_k*16+15];
    PW_m = (in_data[PW_Start_Address+PW_k*16+20]*256)+in_data[PW_Start_Address+PW_k*16+21];
    PW_n = (in_data[PW_Start_Address+PW_k*16+22]*256)+in_data[PW_Start_Address+PW_k*16+23];
    PW_o *= 2;
    PW_m *= 2;
    PW_n *= 2;
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
  if ( (PW_l+PW_Start_Address) > PW_in_size )
  {
/*printf ( "NP3 Header bigger than file size (%ld > %ld)\n", PW_l, PW_in_size);*/
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
  /* PW_n is the highest pattern number (*8) */

  /* test track data size */
  PW_k = (in_data[PW_Start_Address+6]*256)+in_data[PW_Start_Address+7];
  if ( (PW_k <= 63) || ((PW_k+PW_l+PW_Start_Address)>PW_in_size))
  {
/*printf ( "#7 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test notes */
  /* re-calculate the number of sample */
  /* PW_k is the track data size */
  PW_j = ((in_data[PW_Start_Address]<<4)&0xf0)|((in_data[PW_Start_Address+1]>>4)&0x0f);
  for ( PW_m=0 ; PW_m < PW_k ; PW_m++ )
  {
    if ( (in_data[PW_Start_Address+PW_l+PW_m]&0x80) == 0x80 )
      continue;
    /* si note trop grande et si effet = A */
    if ( (in_data[PW_Start_Address+PW_l+PW_m] > 0x49)||
	 ((in_data[PW_Start_Address+PW_l+PW_m+1]&0x0f) == 0x0A) )
    {
/*printf ( "#8,1 Start:%ld (at %x)(PW_k:%x)(PW_l:%x)(PW_m:%x)\n" , PW_Start_Address,PW_Start_Address+PW_l+PW_m,PW_k,PW_l,PW_m );*/
      return BAD;
    }
    /* si effet D et arg > 0x64 */
    if ( ((in_data[PW_Start_Address+PW_l+PW_m+1]&0x0f) == 0x0D )&&
	 (in_data[PW_Start_Address+PW_l+PW_m+2] > 0x64 ) )
    {
/*printf ( "#8,2 Start:%ld (at %ld)(effet:%x)(arg:%x)\n"
          , PW_Start_Address
          , PW_Start_Address+PW_l+PW_m
          , in_data[PW_Start_Address+PW_l+PW_m+1]&0x0f
          , in_data[PW_Start_Address+PW_l+PW_m+2] );*/
      return BAD;
    }
    /* sample nbr > ce qui est defini au debut ? */
/*    if ( (((in_data[PW_Start_Address+PW_l+PW_m]<<4)&0x10)|
	 ((in_data[PW_Start_Address+PW_l+PW_m+1]>>4)&0x0f)) > PW_j )
    {
printf ( "#8,1 Start:%ld (at %x)(PW_k:%x)(PW_l:%x)(PW_m:%x)(PW_j:%ld),(smp nbr given:%d)\n"
, PW_Start_Address
,PW_Start_Address+PW_l+PW_m
,PW_k
,PW_l
,PW_m
,PW_j,
(((in_data[PW_Start_Address+PW_l+PW_m]<<4)&0x10)| ((in_data[PW_Start_Address+PW_l+PW_m+1]>>4)&0x0f)));

      return BAD;
    }*/
    /* all is empty ?!? ... cannot be ! */
    if ( (in_data[PW_Start_Address+PW_l+PW_m]   == 0) &&
         (in_data[PW_Start_Address+PW_l+PW_m+1] == 0) &&
         (in_data[PW_Start_Address+PW_l+PW_m+2] == 0) && (PW_m<(PW_k-3)) )
    {
/*printf ( "#8,3 Start:%ld (at %x)(PW_k:%x)(PW_l:%x)(PW_m:%x)(PW_j:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_l+PW_m,PW_k,PW_l,PW_m,PW_j );*/
      return BAD;
    }
    PW_m += 2;
  }

  /* PW_WholeSampleSize is the size of the sample data */
  /* PW_l is the size of the header 'til the track datas */
  /* PW_k is the size of the track datas */
  return GOOD;
}



void Rip_Noisepacker3 ( void )
{
  OutputSize = PW_k + PW_WholeSampleSize + PW_l;
  /*  printf ( "\b\b\b\b\b\b\b\bNoisePacker v3 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "NoisePacker v3 module", Noisepacker3 );
  
  if ( Save_Status == GOOD )
    PW_i += 16;  /* 15 should do but call it "just to be sure" :) */
}



/*
 *   NoisePacker_v3.c   1998 (c) Asle / ReDoX
 *
 * Converts NoisePacked MODs back to ptk
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up and Binary smaller.
 * update : 01/12/99
 *   - removed fopen() and attached funcs.
 * update : 23/08/10
 *   - Whatever wasn't cleaned up when writing patternlist (showed only if one pattern)
 *   - cleaned up a bit
*/
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

  fillPTKtable(poss);

  BZERO ( Track_Addresses , 128*4*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read number of sample */
  Nbr_Smp = ((in_data[Where]<<4)&0xf0) | ((in_data[Where+1]>>4)&0x0f);
  /*printf ( "\nNumber of sample : %d (%x)\n" , Nbr_Smp , Nbr_Smp );*/

  /* write title */
  Whatever = (Uchar *) malloc ( 1084 );
  BZERO ( Whatever , 1084 );
  /*fwrite ( Whatever , 20 , 1 , out );*/

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
    /*fwrite ( Whatever , 22 , 1 , out );*/
    /* size */
    Whatever[(i*30)+42] = in_data[Where+6];
    Whatever[(i*30)+43] = in_data[Where+7];
    /*fwrite ( &in_data[Where+6] , 2 , 1 , out );*/
    WholeSampleSize += (((in_data[Where+6]*256)+in_data[Where+7])*2);
    /* write finetune,vol */
    Whatever[(i*30)+44] = in_data[Where];
    Whatever[(i*30)+45] = in_data[Where+1];
    /*fwrite ( &in_data[Where] , 2 , 1 , out );*/
    /* write loop start */
    Whatever[(i*30)+46] = in_data[Where+14];
    Whatever[(i*30)+47] = in_data[Where+15];
    /*fwrite ( &in_data[Where+14] , 2 , 1 , out );*/
    /* write loop size */
    Whatever[(i*30)+48] = in_data[Where+12];
    Whatever[(i*30)+49] = in_data[Where+13];
    /*fwrite ( &in_data[Where+12] , 2 , 1 , out );*/
    Where += 16;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* write size of pattern list */
  Whatever[950] = Nbr_Pos;
  /*fwrite ( &Nbr_Pos , 1 , 1 , out );*/

  /* write noisetracker byte */
  Whatever[951] = 0x7f;
  /*c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );*/

  /* bypass 2 bytes ... seems always the same as in $02 */
  /* & bypass 2 other bytes which meaning is beside me */
  Where += 4;

  /* read pattern table */
  Pat_Max = 0x00;
  for ( i=0 ; i<Nbr_Pos ; i++ )
  {
    Whatever[i+952] = ((in_data[Where+(i*2)]*256)+in_data[Where+(i*2)+1])/8;
    /*printf ( "%d," , Whatever[i+952] );*/
    if ( Whatever[i+952] > Pat_Max )
      Pat_Max = Whatever[i+952];
  }
  Where += Nbr_Pos*2;
  Pat_Max += 1;
  /*printf ( "Number of pattern : %d\n" , Pat_Max );*/

  /* write pattern table */
  /*fwrite ( Whatever , 128 , 1 , out );*/

  /* write ptk's ID */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';
  /*fwrite ( Whatever , 4 , 1 , out );*/
  fwrite ( Whatever , 1084 , 1 , out );

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
    BZERO ( Whatever , 1084 );
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
