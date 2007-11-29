/*
 * 27 dec 2001 : added some checks to prevent readings outside of
 * the input file.
*/

/* testSTARPACK() */
/* Rip_STARPACK() */
/* Depack_STARPACK() */

#include "globals.h"
#include "extern.h"


short testSTARPACK ( void )
{
  /* test 1 */
  if ( (PW_i < 23) || ((PW_i+269-23)>=PW_in_size) )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-23;
  PW_l = (in_data[PW_Start_Address+268]*256)+in_data[PW_Start_Address+269];
  PW_k = PW_l/4;
  if ( (PW_k*4) != PW_l )
  {
/*printf ( "#2,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_k>127 )
  {
/*printf ( "#2,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k==0) || ((PW_Start_Address+784)>PW_in_size) )
  {
/*printf ( "#2,2 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  if ( in_data[PW_Start_Address+784] != 0 )
  {
/*printf ( "#3,-1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3  smp size < loop start + loop size ? */
  /* PW_l is still the size of the pattern list */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (((in_data[PW_Start_Address+20+PW_k*8]*256)+in_data[PW_Start_Address+21+PW_k*8])*2);
    PW_m = (((in_data[PW_Start_Address+24+PW_k*8]*256)+in_data[PW_Start_Address+25+PW_k*8])*2)
                        +(((in_data[PW_Start_Address+26+PW_k*8]*256)+in_data[PW_Start_Address+27+PW_k*8])*2);
    if ( (PW_j+2) < PW_m )
    {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }

  /* test #4  finetunes & volumes */
  /* PW_l is still the size of the pattern list */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+22+PW_k*8]>0x0f) || (in_data[PW_Start_Address+23+PW_k*8]>0x40) )
    {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test #5  pattern addresses > sample address ? */
  /* PW_l is still the size of the pattern list */
  /* get sample data address */
  if ( (PW_Start_Address + 0x314) > PW_in_size )
  {
/*printf ( "#5,-1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_k gets address of sample data */
  PW_k = (in_data[PW_Start_Address+784]*256*256*256)
    +(in_data[PW_Start_Address+785]*256*256)
    +(in_data[PW_Start_Address+786]*256)
    +in_data[PW_Start_Address+787];
  if ( (PW_k+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5,0 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_k < 788 )
  {
/*printf ( "#5,1 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_k is the address of the sample data */
  /* pattern addresses > sample address ? */
  for ( PW_j=0 ; PW_j<PW_l ; PW_j+=4 )
  {
    /* PW_m gets each pattern address */
    PW_m = (in_data[PW_Start_Address+272+PW_j]*256*256*256)
      +(in_data[PW_Start_Address+273+PW_j]*256*256)
      +(in_data[PW_Start_Address+274+PW_j]*256)
      +in_data[PW_Start_Address+275+PW_j];
    if ( PW_m > PW_k )
    {
/*printf ( "#5,2 (Start:%ld) (smp addy:%ld) (pat addy:%ld) (pat nbr:%ld) (max:%ld)\n"
         , PW_Start_Address 
         , PW_k
         , PW_m
         , (PW_j/4)
         , PW_l );*/
      return BAD;
    }
  }
  /* test last patterns of the pattern list == 0 ? */
  PW_j += 2;
  while ( PW_j<128 )
  {
    PW_m = (in_data[PW_Start_Address+272+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+273+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+274+PW_j*4]*256)
      +in_data[PW_Start_Address+275+PW_j*4];
    if ( PW_m != 0 )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 1;
  }


  /* test pattern data */
  /* PW_k is the address of the sample data */
  PW_j = PW_Start_Address + 788;
  /* PW_j points on pattern data */
/*printf ( "PW_j:%ld , PW_k:%ld\n" , PW_j , PW_k );*/
  while ( PW_j<(PW_k+PW_Start_Address-4) )
  {
    if ( in_data[PW_j] == 0x80 )
    {
      PW_j += 1;
      continue;
    }
    if ( in_data[PW_j] > 0x80 )
    {
/*printf ( "#6 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* empty row ? ... not possible ! */
    if ( (in_data[PW_j]   == 0x00) &&
         (in_data[PW_j+1] == 0x00) &&
         (in_data[PW_j+2] == 0x00) &&
         (in_data[PW_j+3] == 0x00) )
    {
/*printf ( "#6,0 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx = C .. arg > 64 ? */
    if ( ((in_data[PW_j+2]*0x0f)==0x0C) && (in_data[PW_j+3]>0x40) )
    {
/*printf ( "#6,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx = D .. arg > 64 ? */
    if ( ((in_data[PW_j+2]*0x0f)==0x0D) && (in_data[PW_j+3]>0x40) )
    {
/*printf ( "#6,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_j += 4;
  }

  return GOOD;
}




void Rip_STARPACK ( void )
{
  /* PW_k is still the sample data address */
  /* PW_WholeSampleSize is the whole sample size already */

  OutputSize = PW_WholeSampleSize + PW_k + 0x314;

  CONVERT = GOOD;
  Save_Rip ( "StarTrekker Packer module", Star_pack );
  
  if ( Save_Status == GOOD )
    PW_i += 24;  /* 23 after 1st vol */
}




/*
 *  StarTrekker _Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk StarTrekker packed MODs
 *
*/
void Depack_STARPACK ( void )
{
  Uchar c1=0x00,c2=0x00;
  Uchar Pat_Pos;
  Uchar *Whatever;
  Uchar *Pattern;
  long i=0,j=0,k=0;
  long Total_Sample_Size=0;
  long Pats_Address[128];
  long Read_Pats_Address[128];
  long SampleDataAddress=0;
  long Where = PW_Start_Address;
  long MaxPatAddy=0;
  FILE /**in,*/*out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Pats_Address , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );


  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever, 1024);

  /* read and write title */
  fwrite (&in_data[Where], 20, 1, out );
  Where += 20;

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( &Whatever[0], 22, 1, out );
    /*sample name*/

    Total_Sample_Size += (((in_data[Where+i*8]*256)+in_data[Where+1+i*8])*2);
    fwrite ( &in_data[Where+i*8], 8, 1, out );
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/
  Where = PW_Start_Address + 268;

  /* read size of pattern table */
  Pat_Pos = ((in_data[Where]*256)+in_data[Where+1])/4;
  Where += 4;
  fwrite ( &Pat_Pos, 1, 1, out );
  /*printf ( "Size of pattern table : %d\n" , Pat_Pos );*/
  /* bypass $0000 unknown bytes */

/***********/

  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    Where+=4;
    if ( Pats_Address[i] > MaxPatAddy )
      MaxPatAddy = Pats_Address[i];
  }


  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

/***********/


  /* read sample data address */
  Where = PW_Start_Address + 0x310;
  SampleDataAddress = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3]+0x314;

  /* pattern data */
  Where += 4;
  /*PatMax += 1;*/

  c1=0; /* will count patterns */
  k=0; /* current note number */
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  for ( j=0 ; j<(MaxPatAddy+0x400) ; j+=4 )
  {
    if ( (i%1024) == 0 )
    {
      if ( j>MaxPatAddy )
	break;
      Read_Pats_Address[c1] = j;
      c1 += 0x01;
    }
    if (in_data[Where+j] == 0x80 )
    {
      j -= 3;
      i += 4;
      continue;
    }

    c2 = ((in_data[Where+j]) | ((in_data[Where+j+2]>>4)&0x0f)) / 4;

    Pattern[i]   = in_data[Where+j] & 0x0f;
    Pattern[i]  |= (c2&0xf0);
    Pattern[i+1] = in_data[Where+j+1];
    Pattern[i+2] = in_data[Where+j+2] & 0x0f;
    Pattern[i+2]|= ((c2<<4)&0xf0);
    Pattern[i+3] = in_data[Where+j+3];

    i += 4;
  }
  /* Where should be now on the sample data addy */

  /* write pattern list */
  /* write pattern table */
  BZERO ( Whatever, 128 );
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<128 ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
      {
	Whatever[c2] = (Uchar) i;
	break;
      }
  fwrite ( &Whatever[0], 128, 1, out );

  /* write ptk's ID */
  Whatever[0] = 'M';Whatever[1] = '.';Whatever[2] = 'K';
  fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  fwrite ( &Whatever[2] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  free (Whatever);

  /* write pattern data */
  /* c1 is still the number of patterns stored */
  fwrite ( Pattern, c1*1024, 1, out );

  /* sample data */
  Where = PW_Start_Address + SampleDataAddress;

  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );


  Crap ( " StarTrekker Pack " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
