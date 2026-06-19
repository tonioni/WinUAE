/*
 * newtronold.c ... 21 jan 2007
 
 Newtron Old format
Created by ?!?
Analysed by Sylvain "Asle" Chipaux (asle@free.fr)

Source :
 - Little Joe & Newtron Musicdisk


Offset    size (byte)    Comment
------    -----------    -------

 0             2         patternlist address (-8) [A]
 2             2         size of patternlist [B]
 4             4         ?

      **************************************
      * the following is repeated [A]/8 times *
      * with 8 bytes description for 1 smp *
******************************************************
                                                     *
 8             2         Sample Size / 2             *
 10            1         Finetune (0 -> F)           *
 11            1         Volume (0 - 40h)            *
 12            2         Loop Start / 2              *
 14            2         Loop Size / 2               *
                                                     *
******************************************************

[A]           [B]        Pattern table

[A]+[B]        ?         Pattern datas
                         (Pattern datas are stored like Ptk)
                         (one pattern is 1024 ($400) bytes).


 Follow the Sample datas stored like ProTracker.
Nothing is packed..
 
*/
/* testNewtronOld() */
/* Rip_NewtronOld() */
/* Depack_NewtronOld() */


#include "globals.h"
#include "extern.h"

/*
 * additional tests - 20100207
*/
int16_t	 testNewtronOld ( void )
{
  /* test #1 */
  if ( (PW_i < 11) || ((PW_i+6+1+1024+2)>PW_in_size))
  {
    return BAD;
  }
  
  /* test #1.5 */
  PW_Start_Address = PW_i-11;
  if ( in_data[PW_Start_Address+2] != 0x00 )
  {
    return BAD;
  }

  PW_l=(in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1]+8;
  /* test #2 samples */
  PW_WholeSampleSize = 0;
  PW_l = (PW_l/8)-1;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j+=1 )
  {
    /* size */
    PW_k = (((in_data[PW_Start_Address+8+8*PW_j]*256)+in_data[PW_Start_Address+9+8*PW_j])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+12+8*PW_j]*256)+in_data[PW_Start_Address+13+8*PW_j])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+14+8*PW_j]*256)+in_data[PW_Start_Address+15+8*PW_j])*2);
    PW_WholeSampleSize += PW_k;

    if ( test_smps(PW_k, PW_m, PW_n, in_data[PW_Start_Address+11+8*PW_j], in_data[PW_Start_Address+10+8*PW_j] ) == BAD )
    {
            /*printf ( "#2 (start:%ld),(siz:%ld)(loopstart:%ld)(lsiz:%ld)(vol:%d)(fine:%d)(where:%ld)(PW_j:%ld)\n"
	       ,PW_Start_Address,PW_k,PW_m,PW_n, in_data[PW_Start_Address+11+8*PW_j], in_data[PW_Start_Address+10+8*PW_j]
	       ,PW_j*8+8+PW_Start_Address,PW_j );*/
      return BAD; 
    }
    if ( (PW_k==0) && (PW_m==0) && (PW_n==0))
      return BAD;
  }

  if ( PW_WholeSampleSize <= 2 )
  {
    /*printf(  "#3\n" );*/
    return BAD;
  }

  /* test #4 - patternlist size */
  PW_l = in_data[PW_Start_Address+3];
  if ( (PW_l > 0x7f) || (PW_l == 0x00) )
  {
    /*printf(  "#4 (start:%ld)(indata[0]:%x)\n",PW_Start_Address,in_data[PW_Start_Address] );*/
    return BAD;
  }

  /* test #5 */
  /* PW_l is the size of the patternlist */
  PW_k = (in_data[PW_Start_Address]*256)+in_data[PW_Start_Address+1]+8;
  /* PW_k is the patternlist addy */
  if (((PW_k/8)*8) != PW_k)
  {
    /*printf(  "#5.0\n" );*/
    return BAD;
  }
  
  PW_m = 0;
  for ( PW_j=0 ; PW_j<PW_l ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_k+PW_j] > PW_m )
      PW_m = in_data[PW_Start_Address+PW_k+PW_j];
    if ( in_data[PW_Start_Address+PW_k+PW_j] > 0x7f )
    {
      /*printf(  "#5.1 (PW_l:%lx)(PW_k:%lx)(Where:%ld)(val:%x)\n",PW_l,PW_k,PW_Start_Address+PW_k+PW_j,in_data[PW_Start_Address+PW_k+PW_j]);*/
      return BAD;
    }
  }
  PW_m += 1;

  /* #6 */
  /* PW_l is the size of the patternlist */
  /* PW_m contains the number of pattern saved */
  if ( ((PW_m*1024) + PW_k + PW_l + PW_Start_Address) > PW_in_size)
  {
    return BAD;
  }

  /* test #7  ptk notes .. gosh ! (testing all patterns !) */
  /* PW_k is the patternlist addy */
  /* PW_l is the size of the patternlist */
  /* PW_m contains the number of pattern saved */
  /* PW_WholeSampleSize is the whole sample size */
  PW_k += PW_l;
  /* PW_k is now the pat data addy */
  /*printf ( "\nPW_k:%lx\n",PW_k );*/
  for ( PW_j=0 ; PW_j<(256*PW_m) ; PW_j++ )
  {
    unsigned char c = in_data[PW_Start_Address+PW_k+(PW_j*4)];
    if ((c&0x0f) > 0x03)
    {
      /*printf(  "#7,1 (start:%ld)(where:%lx)(c:%x)\n",PW_Start_Address,PW_Start_Address+PW_k+(PW_j*4),c );*/
      return BAD;
    }
    if ((c&0xf0) > 0x10)
    {
      /*printf(  "#7,2 (start:%ld)(where:%lx)(c:%x)\n",PW_Start_Address,PW_Start_Address+PW_k+(PW_j*4),c );*/
      return BAD;
    }
    PW_n  = in_data[PW_Start_Address+PW_k+PW_j*4]&0x0f;
    PW_n *= 256;
    PW_n += in_data[PW_Start_Address+PW_k+1+PW_j*4];
    if ( (PW_n > 0) && (PW_n<0x71) )
    {
      /*printf ( "#7,1 (Start:%ld)(where:%ld)(note:%ld)\n" , PW_Start_Address,PW_Start_Address+PW_k+PW_j*4, PW_WholeSampleSize );*/
      return BAD;
    }
  }

  return GOOD;
}



void Rip_NewtronOld ( void )
{
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_m contains the number of pattern saved */
  /* PW_k is the pat data addy */

  OutputSize = PW_k + PW_WholeSampleSize + (PW_m*1024);

  CONVERT = GOOD;
  Save_Rip ( "Newtron module Old", NewtronOld );
  
  if ( Save_Status == GOOD )
    PW_i += 11;
}

/*
 *   newtronold.c   2007 (c) Asle / ReDoX
 *
 * Converts Newtron Old packed MODs back to PTK MODs
 *
 * clean up - 20100207
*/

void Depack_NewtronOld ( void )
{
  uint8_t *Whatever;
  int32_t	 i=0,j=0;
  int32_t	 Total_Sample_Size=0;
  int32_t	 Where = PW_Start_Address;
  int32_t	 patlistaddy=0;
  uint8_t patsize = 0;
  uint8_t max=0x00;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (uint8_t *) malloc (1085);
  BZERO ( Whatever , 1085 );

  /* title */

  /* size of header */
  patlistaddy = (in_data[Where]*256)+in_data[Where+1]+8;
  /* nbr of samples */
  j = (patlistaddy/8)-1;
  
  Where += 8;
  
  for ( i=0 ; i<j ; i++ )
  {
    Whatever[20+30*i+22] = in_data[Where];
    Whatever[20+30*i+23] = in_data[Where+1];
    Whatever[20+30*i+24] = in_data[Where+2];
    Whatever[20+30*i+25] = in_data[Where+3];
    Whatever[20+30*i+26] = in_data[Where+4];
    Whatever[20+30*i+27] = in_data[Where+5];
    Whatever[20+30*i+28] = in_data[Where+6];
    Whatever[20+30*i+29] = in_data[Where+7];
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    Where += 8;
  }
  while (i<31)
  {
    Whatever[20+30*i+29] = 0x01;
    i++;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* pattern table lenght & Ntk byte */
  patsize = in_data[PW_Start_Address+3];
  Whatever[950] = patsize;
  Whatever[951] = 0x7f;

  Where = patlistaddy+PW_Start_Address;
  for ( i=0 ; i<patsize ; i++ )
  {
    if ( in_data[Where+i] > max )
      max = in_data[Where+i];
    Whatever[952+i] = in_data[Where+i];
  }
  Where += patsize;
  /*printf ( "Number of pattern : %d\n" , Max+1 );*/

  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';
  fwrite ( Whatever , 1084 , 1 , out );

  /* pattern data */
  i = (max+1)*1024;
  fwrite ( &in_data[Where] , i , 1 , out );
  Where += i;
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "    Newtron old   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
