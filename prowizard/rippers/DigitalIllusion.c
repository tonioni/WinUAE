/* (3rd of April 2000)
 *   bugs pointed out by Thomas Neumann .. thx :)
 * (May 2002)
 *   added test_smps()
*/
/* testDI() */
/* Rip_DI() */
/* Depack_DI() */


#include "globals.h"
#include "extern.h"


short testDI ( void )
{
  /* test #1 */
  if ( PW_i < 17 )
  {
    return BAD;
  }

  /* test #2  (number of sample) */
  PW_Start_Address = PW_i-17;
  PW_k = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1];
  if ( PW_k > 31 )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3 (finetunes and whole sample size) */
  /* PW_k = number of samples */
  PW_WholeSampleSize = 0;
  PW_l = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_o = (((in_data[PW_Start_Address+(PW_j*8)+14]*256)+in_data[PW_Start_Address+(PW_j*8)+15])*2);
    PW_m = (((in_data[PW_Start_Address+(PW_j*8)+18]*256)+in_data[PW_Start_Address+(PW_j*8)+19])*2);
    PW_n = (((in_data[PW_Start_Address+(PW_j*8)+20]*256)+in_data[PW_Start_Address+(PW_j*8)+21])*2);
    if ( (PW_o > 0xffff) ||
         (PW_m > 0xffff) ||
         (PW_n > 0xffff) )
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( test_smps(PW_o, PW_m, PW_n, in_data[PW_Start_Address+17+PW_j*8], in_data[PW_Start_Address+16+PW_j*8] ) == BAD )
      return BAD;
    /* gets total size of samples */
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 2 )
  {
/*printf ( "#2,4\n" );*/
    return BAD;
  }

  /* test #4 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the whole sample size */
  /* PW_k is still the number of sample */
  PW_m = PW_k;
  PW_j = (in_data[PW_Start_Address+2]*256*256*256)
         +(in_data[PW_Start_Address+3]*256*256)
         +(in_data[PW_Start_Address+4]*256)
         +in_data[PW_Start_Address+5];
  /* PW_j is the address of pattern table now */
  PW_k = (in_data[PW_Start_Address+6]*256*256*256)
         +(in_data[PW_Start_Address+7]*256*256)
         +(in_data[PW_Start_Address+8]*256)
         +in_data[PW_Start_Address+9];
  /* PW_k is the address of the pattern data */
  PW_l = (in_data[PW_Start_Address+10]*256*256*256)
         +(in_data[PW_Start_Address+11]*256*256)
         +(in_data[PW_Start_Address+12]*256)
         +in_data[PW_Start_Address+13];
  /* PW_l is the address of the sample data */
  if ( (PW_k <= PW_j)||(PW_l<=PW_j)||(PW_l<=PW_k) )
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k-PW_j) > 128 )
  {
/*printf ( "#3,1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( (PW_k > PW_in_size)||(PW_l>PW_in_size)||(PW_j>PW_in_size) )
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #4,1 :) */
  PW_m *= 8;
  PW_m += 2;
  if ( PW_j < PW_m )
  {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #5 */
  if ( (PW_k + PW_Start_Address) > PW_in_size )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern table reliability */
  for ( PW_m=PW_j ; PW_m<(PW_k-1) ; PW_m++ )
  {
    if ( in_data[PW_Start_Address + PW_m] > 0x80 )
    {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  /* test #6  ($FF at the end of pattern list ?) */
  if ( in_data[PW_Start_Address+PW_k-1] != 0xFF )
  {
/*printf ( "#6,1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #7 (addres of sample data > $FFFF ? ) */
  /* PW_l is still the address of the sample data */
  if ( PW_l > 65535 )
  {
/*printf ( "#7 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}


void Rip_DI ( void )
{
  /*PW_WholeSampleSize is already the whole sample size */

  PW_j = (in_data[PW_Start_Address+10]*256*256*256)
        +(in_data[PW_Start_Address+11]*256*256)
        +(in_data[PW_Start_Address+12]*256)
        +in_data[PW_Start_Address+13];

  OutputSize = PW_WholeSampleSize + PW_j;

  CONVERT = GOOD;
  Save_Rip ( "Digital Illusion Packed music", Digital_illusion );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 15);  /* 14 should do but call it "just to be sure" :) */
}


/*
 *   Digital_Illusion.c   1997 (c) Asle / ReDoX
 *
 * Converts DI packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 * 20051002 : testing fopen()
*/
void Depack_DI ( void )
{
  Uchar Note,Smp,Fx,FxVal;
  Uchar poss[37][2];
  Uchar *Whatever;
  long i=0,k=0;
  Ushort Pattern_Addresses_Table[128];
  long Add_Pattern_Table=0;
  long Add_Pattern_Data=0;
  long Add_Sample_Data=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  BZERO ( Pattern_Addresses_Table , 128*2 );

  /* title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  k = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  /*printf ( "Number of sample : %d\n" , k );*/

  Add_Pattern_Table = (in_data[Where+1]*256*256)+
                      (in_data[Where+2]*256)+
                       in_data[Where+3];
  Where += 4;
  /*printf ( "Pattern table address : %ld\n" , Add_Pattern_Table );*/

  Add_Pattern_Data = (in_data[Where+1]*256*256)+
                     (in_data[Where+2]*256)+
                      in_data[Where+3];
  Where += 4;
  /*printf ( "Pattern data address : %ld\n" , Add_Pattern_Data );*/

  Add_Sample_Data = (in_data[Where+1]*256*256)+
                    (in_data[Where+2]*256)+
                     in_data[Where+3];
  Where += 4;
  /*printf ( "Sample data address : %ld\n" , Add_Sample_Data );*/


  for ( i=0 ; i<k ; i++ )
  {
    fwrite ( Whatever , 22 , 1 , out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  Whatever[29] = 0x01;
  for ( i=k ; i<31 ; i++ )
    fwrite ( Whatever , 30 , 1 , out );

  k = Where;

  Where = PW_Start_Address + Add_Pattern_Table;
  i=0;
  do
  {
    Whatever[200] = in_data[Where++];
    Whatever[i]=Whatever[200];
    i+=1;
  }while ( Whatever[200] != 0xff );
  Whatever[i-1] = 0x00;
  Whatever[257] = i-1;
  fwrite ( &Whatever[257] , 1 , 1 , out );

  Whatever[256] = 0x7f;
  fwrite ( &Whatever[256] , 1 , 1 , out );

  Whatever[256] = 0;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( Whatever[i] > Whatever[256] )
      Whatever[256] = Whatever[i];
  }
  fwrite ( Whatever , 128 , 1 , out );

  /*printf ( "Number of pattern : %d\n" , Whatever[257] );*/
  /*printf ( "Highest pattern number : %d\n" , Whatever[256] );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  Where = k;
  for ( i=0 ; i<=Whatever[256] ; i++ )
  {
    Pattern_Addresses_Table[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
  }

  for ( i=0 ; i<=Whatever[256] ; i++ )
  {
    Where = PW_Start_Address + Pattern_Addresses_Table[i];
    for ( k=0 ; k<256 ; k++ )  /* 256 = 4(voices) * 64(rows) */
    {
      Whatever[0] = Whatever[1] = Whatever[2] = Whatever[3] = 0x00;
      Whatever[10] = in_data[Where++];
      if ( (Whatever[10] & 0x80) == 0 )
      {
        Whatever[11] = in_data[Where++];
	Note = ( ((Whatever[10] << 4) & 0x30) | ( ( Whatever[11]>>4 ) & 0x0f ) );
	Whatever[0] = poss[Note][0];
	Whatever[1] = poss[Note][1];
	Smp = (Whatever[10] >> 2) & 0x1f;
	Whatever[0] |= (Smp & 0xf0);
	Whatever[2] = (Smp << 4) & 0xf0;
	Fx = Whatever[11] & 0x0f;
	Whatever[2] |= Fx;
	FxVal = 0x00;
	Whatever[3] = FxVal;
	fwrite ( Whatever , 4 , 1 , out );
	continue;
      }
      if ( Whatever[10] == 0xff )
      {
        Whatever[0] = Whatever[1] = Whatever[2] = Whatever[3] = 0x00;
	fwrite ( Whatever , 4 , 1 , out );
	continue;
      }
      Whatever[11] = in_data[Where++];
      Whatever[12] = in_data[Where++];
      Note = ( ((Whatever[10] << 4) & 0x30) | ( (Whatever[11] >> 4) & 0x0f ) );
      Whatever[0] = poss[Note][0];
      Whatever[1] = poss[Note][1];
      Smp = (Whatever[10] >> 2) & 0x1f;
      Whatever[0] |= (Smp & 0xf0);
      Whatever[2] = (Smp << 4) & 0xf0;
      Fx = Whatever[11] & 0x0f;
      Whatever[2] |= Fx;
      FxVal = Whatever[12];
      Whatever[3] = FxVal;
      fwrite ( Whatever , 4 , 1 , out );
    }
  }
  free ( Whatever );


  Where = PW_Start_Address + Add_Sample_Data;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " Digital Illusion " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */

}
