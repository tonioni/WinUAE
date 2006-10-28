/* testSoundFX13() */
/* Rip_SoundFX13() */
/* Depack_SoundFX13() */


#include "globals.h"
#include "extern.h"

short testSoundFX13 ( void )
{
  /* test 1 */
  if ( PW_i < 0x3C )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  /* samples tests */
  PW_Start_Address = PW_i-0x3C;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    /* size */
    PW_j = ((in_data[PW_Start_Address+PW_k*4+2]*256)+in_data[PW_Start_Address+PW_k*4+3]);
    /* loop start */
    PW_m = ((in_data[PW_Start_Address+106+PW_k*30]*256)+in_data[PW_Start_Address+107+PW_k*30]);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+108+PW_k*30]*256)+in_data[PW_Start_Address+109+PW_k*30])*2);
    /* all sample sizes */

    /* size,loopstart,replen > 64k ? */
    if ( (PW_j > 0xFFFF) || (PW_m > 0xFFFF) || (PW_n > 0xFFFF) )
    {
/*printf ( "#2,0 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* replen > size ? */
    if ( PW_n > (PW_j+2) )
    {
/*printf ( "#2 (Start:%ld) (smp:%ld) (size:%ld) (replen:%ld)\n"
         , PW_Start_Address , PW_k+1 , PW_j , PW_n );*/
      return BAD;
    }
    /* loop start > size ? */
    if ( PW_m > PW_j )
    {
/*printf ( "#2,0 (Start:%ld) (smp:%ld) (size:%ld) (lstart:%ld)\n"
         , PW_Start_Address , PW_k+1 , PW_j , PW_m );*/
      return BAD;
    }
    /* loop size =0 & loop start != 0 ?*/
    if ( (PW_m != 0) && (PW_n==0) )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* size & loopstart !=0 & size=loopstart ? */
    if ( (PW_j != 0) && (PW_j==PW_m) )
    {
/*printf ( "#2,15 (start:%ld) (smp:%ld) (siz:%ld) (lstart:%ld)\n"
         , PW_Start_Address,PW_k+1,PW_j,PW_m );*/
      return BAD;
    }
    /* size =0 & loop start !=0 */
    if ( (PW_j==0) && (PW_m!=0) )
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* get real whole sample size */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<15 ; PW_j++ )
  {
    PW_k = ((in_data[PW_Start_Address+PW_j*4]*256*256*256)+
            (in_data[PW_Start_Address+PW_j*4+1]*256*256)+
            (in_data[PW_Start_Address+PW_j*4+2]*256)+
             in_data[PW_Start_Address+PW_j*4+3] );
    if ( PW_k > 131072 )
    {
/*printf ( "#2,4 (start:%ld) (smp:%ld) (size:%ld)\n"
         , PW_Start_Address,PW_j,PW_k );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_k;
  }

  /* test #3  finetunes & volumes */
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+105+PW_k*30]>0x40 )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+0x212];
  if ( (PW_l>127) || (PW_l==0) )
  {
/*printf ( "#4,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+0x214+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+0x214+PW_j];
    if ( in_data[PW_Start_Address+0x214+PW_j] > 127 )
    {
/*printf ( "#4,1 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;


  /* test #5 pattern data ... */
  if ( ((PW_k*1024)+0x294+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;                                         
}



void Rip_SoundFX13 ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is the WholeSampleSize :) */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 0x294;

  CONVERT = BAD;

  Save_Rip ( "Sound FX 1.3 module", SoundFX );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 0x40);  /* 0x3C should do but call it "just to be sure" :) */
}



/*
 *   SoundFX.c   1999 (c) Sylvain "Asle" Chipaux
 *
 * Depacks musics in the SoundFX format and saves in ptk.
 *
*/

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
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

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
