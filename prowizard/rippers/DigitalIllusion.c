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


int16_t	 testDI ( void )
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
 * 20100119 : cleaned up a bit - header is one fwrite.
*/
void Depack_DI ( void )
{
  uint8_t Note,Smp,Fx,FxVal;
  uint8_t poss[37][2];
  uint8_t *Whatever, c1;
  int32_t	 i=0,k=0;
  uint16_t	 Pattern_Addresses_Table[128];
  int32_t	 Add_Pattern_Table=0;
  int32_t	 Add_Pattern_Data=0;
  int32_t	 Add_Sample_Data=0;
  int32_t	 Total_Sample_Size=0;
  int32_t	 Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  BZERO ( Pattern_Addresses_Table , 128*2 );

  /* title */
  Whatever = (uint8_t *) malloc (1085);
  BZERO ( Whatever , 1085 );

  k = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  /*printf ( "Number of sample : %d\n" , k );*/

  Add_Pattern_Table = (in_data[Where+1]*256*256)+
                      (in_data[Where+2]*256)+
                       in_data[Where+3];
  Where += 4;
  /*printf ( "Pattern table address : %d\n" , Add_Pattern_Table );*/

  Add_Pattern_Data = (in_data[Where+1]*256*256)+
                     (in_data[Where+2]*256)+
                      in_data[Where+3];
  Where += 4;
  printf ( "Pattern data address : %d\n" , Add_Pattern_Data );

  Add_Sample_Data = (in_data[Where+1]*256*256)+
                    (in_data[Where+2]*256)+
                     in_data[Where+3];
  Where += 4;
  printf ( "Sample data address : %d\n" , Add_Sample_Data );


  for ( i=0 ; i<k ; i++ )
  {
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    Whatever[i*30 + 42] = in_data[Where];
    Whatever[i*30 + 43] = in_data[Where+1];
    Whatever[i*30 + 44] = in_data[Where+2];
    Whatever[i*30 + 45] = in_data[Where+3];
    Whatever[i*30 + 46] = in_data[Where+4];
    Whatever[i*30 + 47] = in_data[Where+5];
    Whatever[i*30 + 48] = in_data[Where+6];
    Whatever[i*30 + 49] = in_data[Where+7];
    Where += 8;
  }
  /*printf ( "Whole sample size : %d\n" , Total_Sample_Size );*/

  for ( i=k ; i<31 ; i++ )
    Whatever[i*30 + 49] = 0x01;;

  k = Where;

  Where = PW_Start_Address + Add_Pattern_Table;
  i=0;
  c1 = 0x00;
  while ( in_data[Where] != 0xff )
  {
    Whatever[i+952]=in_data[Where];
    if (in_data[Where] > c1)
      c1 = in_data[Where];
    i+=1;Where += 1;
  }
  Whatever[950] = (uint8_t)i;
  Whatever[951] = 0x7f;

  printf ( "\nHighest pattern number : %d (Where:%x)\n" , c1,Where );

  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';

  fwrite ( Whatever , 1084 , 1 , out );


  Where = k;
  for ( i=0 ; i<=c1 ; i++ )
  {
    Pattern_Addresses_Table[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
  }

  for ( i=0 ; i<=c1 ; i++ )
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
