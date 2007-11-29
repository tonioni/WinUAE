/* testPP30() */
/* Rip_PP30() */
/* Depack_PP30() */

#include "globals.h"
#include "extern.h"


short testPP30 ( void )
{
  /* test #1 */
  if ( (PW_i < 3) || ((PW_i+891)>=PW_in_size))
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-3;
  PW_WholeSampleSize=0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    PW_k = (((in_data[PW_Start_Address+PW_j*8]*256)+in_data[PW_Start_Address+PW_j*8+1])*2);
    PW_WholeSampleSize += PW_k;
    /* finetune > 0x0f ? */
    if ( in_data[PW_Start_Address+8*PW_j+2] > 0x0f )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* volume > 0x40 ? */
    if ( in_data[PW_Start_Address+8*PW_j+3] > 0x40 )
    {
/*printf ( "#2,0 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( (((in_data[PW_Start_Address+4+PW_j*8]*256)+in_data[PW_Start_Address+5+PW_j*8])*2) > PW_k )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3   about size of pattern list */
  PW_l = in_data[PW_Start_Address+248];
  if ( (PW_l > 127) || (PW_l==0) )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get the highest track value */
  PW_k=0;
  for ( PW_j=0 ; PW_j<512 ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+250+PW_j];
    if ( PW_l>PW_k )
      PW_k = PW_l;
  }
  /* PW_k is the highest track number */
  PW_k += 1;
  PW_k *= 64;

  /* test #4  track data value *4 ? */
  /* PW_WholeSampleSize is the whole sample size */
  PW_m = 0;
  if ( ((PW_k*2)+PW_Start_Address+763) > PW_in_size )
  {
    return BAD;
  }
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_l = (in_data[PW_Start_Address+762+PW_j*2]*256)+in_data[PW_Start_Address+763+PW_j*2];
    if ( PW_l > PW_m )
      PW_m = PW_l;
    if ( ((PW_l*4)/4) != PW_l  )
    {
/*printf ( "#4 (start:%ld)(where:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_j*2+762 );*/
      return BAD;
    }
  }

  /* test #5  reference table size *4 ? */
  /* PW_m is the highest reference number */
  PW_k *= 2;
  PW_m /= 4;
  PW_l = (in_data[PW_Start_Address+PW_k+762]*256*256*256)
    +(in_data[PW_Start_Address+PW_k+763]*256*256)
    +(in_data[PW_Start_Address+PW_k+764]*256)
    +in_data[PW_Start_Address+PW_k+765];
  if ( PW_l > 65535 )
  {
    return BAD;
  }
  if ( PW_l != ((PW_m+1)*4) )
  {
/*printf ( "#5 (start:%ld)(where:%ld)\n" , PW_Start_Address,(PW_Start_Address+PW_k+762) );*/
    return BAD;
  }

  /* test #6  data in reference table ... */
  for ( PW_j=0 ; PW_j<(PW_l/4) ; PW_j++ )
  {
    /* volume > 41 ? */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0c) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x41 ) )
    {
/*printf ( "#6 (vol > 40 at : %ld)\n" , PW_Start_Address+PW_k+766+PW_j*4+2 );*/
      return BAD;
    }
    /* break > 40 ? */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0d) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x40 ) )
    {
/*printf ( "#6,1\n" );*/
      return BAD;
    }
    /* jump > 128 */
    if ( ((in_data[PW_Start_Address+PW_k+766+PW_j*4+2]&0x0f)==0x0b) &&
         (in_data[PW_Start_Address+PW_k+766+PW_j*4+3] > 0x7f ) )
    {
/*printf ( "#6,2\n" );*/
      return BAD;
    }
    /* smp > 1f ? */
    if ((in_data[PW_Start_Address+PW_k+766+PW_j*4]&0xf0)>0x10)
    {
/*printf ( "#6,3\n" );*/
      return BAD;
    }
  }
  /* PW_WholeSampleSize is the whole sample size */

  return GOOD;
}



void Rip_PP30 ( void )
{
  /* PW_k is still the size of the track "data" ! */
  /* PW_WholeSampleSize is still the whole sample size */

  PW_l = (in_data[PW_Start_Address+762+PW_k]*256*256*256)
    +(in_data[PW_Start_Address+763+PW_k]*256*256)
    +(in_data[PW_Start_Address+764+PW_k]*256)
    +in_data[PW_Start_Address+765+PW_k];

  OutputSize = PW_WholeSampleSize + PW_k + PW_l + 766;

  CONVERT = GOOD;
  Save_Rip ( "ProPacker v3.0 module", Propacker_30 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}



/*
 *   ProPacker_30.c   1997 (c) Asle / ReDoX
 *
 * Converts PP30 packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * update : 26/11/1999 by Sylvain "Asle" Chipaux
 *   - reduced to only one FREAD.
 *   - Speed-up and Binary smaller.
 * update : 8 dec 2003
 *   - no more fopen ()
*/

void Depack_PP30 ( void )
{
  Uchar c1=0x00,c2=0x00;
  short Max=0;
  Uchar Tracks_Numbers[4][128];
  short Tracks_PrePointers[512][64];
  Uchar NOP=0x00; /* number of pattern */
  Uchar *ReferenceTable;
  Uchar *Whatever;
  long i=0,j=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Tracks_Numbers , 4*128 );
  BZERO ( Tracks_PrePointers , 512*64 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );
    /* sample siz */
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    /* size,fine,vol,lstart,lsize */
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }

  /* pattern table lenght */
  NOP = in_data[Where];
  fwrite ( &NOP , 1 , 1 , out );
  Where += 1;
  /*printf ( "Number of patterns : %d\n" , NOP );*/

  /* NoiseTracker restart byte */
  fwrite ( &in_data[Where] , 1 , 1 , out );
  Where += 1;

  Max = 0;
  for ( j=0 ; j<4 ; j++ )
  {
    for ( i=0 ; i<128 ; i++ )
    {
      Tracks_Numbers[j][i] = in_data[Where];
      Where += 1;
      if ( Tracks_Numbers[j][i] > Max )
        Max = Tracks_Numbers[j][i];
    }
  }

  /* write pattern table without any optimizing ! */
  for ( c1=0x00 ; c1<NOP ; c1++ )
    fwrite ( &c1 , 1 , 1 , out );
  c2 = 0x00;
  for ( ; c1<128 ; c1++ )
    fwrite ( &c2 , 1 , 1 , out );

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* PATTERN DATA code starts here */

  /*printf ( "Highest track number : %d\n" , Max );*/
  for ( j=0 ; j<=Max ; j++ )
  {
    for ( i=0 ; i<64 ; i++ )
    {
      Tracks_PrePointers[j][i] = ((in_data[Where]*256)+
                                   in_data[Where+1])/4;
      Where += 2;
    }
  }

  /* read "reference table" size */
  j = ((in_data[Where]*256*256*256)+
       (in_data[Where+1]*256*256)+
       (in_data[Where+2]*256)+
        in_data[Where+3]);
  Where += 4;


  /* read "reference Table" */
  ReferenceTable = (Uchar *) malloc ( j );
  for ( i=0 ; i<j ; i++,Where+=1 )
    ReferenceTable[i] = in_data[Where];

  /* NOW, the real shit takes place :) */
  for ( i=0 ; i<NOP ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<64 ; j++ )
    {

      Whatever[j*16]   = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [0][i]] [j]*4];
      Whatever[j*16+1] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [0][i]] [j]*4+1];
      Whatever[j*16+2] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [0][i]] [j]*4+2];
      Whatever[j*16+3] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [0][i]] [j]*4+3];

      Whatever[j*16+4] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [1][i]] [j]*4];
      Whatever[j*16+5] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [1][i]] [j]*4+1];
      Whatever[j*16+6] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [1][i]] [j]*4+2];
      Whatever[j*16+7] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [1][i]] [j]*4+3];

      Whatever[j*16+8] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [2][i]] [j]*4];
      Whatever[j*16+9] = ReferenceTable[Tracks_PrePointers [Tracks_Numbers [2][i]] [j]*4+1];
      Whatever[j*16+10]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [2][i]] [j]*4+2];
      Whatever[j*16+11]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [2][i]] [j]*4+3];

      Whatever[j*16+12]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [3][i]] [j]*4];
      Whatever[j*16+13]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [3][i]] [j]*4+1];
      Whatever[j*16+14]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [3][i]] [j]*4+2];
      Whatever[j*16+15]= ReferenceTable[Tracks_PrePointers [Tracks_Numbers [3][i]] [j]*4+3];

    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( ReferenceTable );
  free ( Whatever );


  /* sample data */
  /*printf ( "Total sample size : %ld\n" , Total_Sample_Size );*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "  ProPacker v3.0  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
