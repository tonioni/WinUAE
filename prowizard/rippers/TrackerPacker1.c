/* testTP1() */
/* Rip_TP1() */
/* Depack_TP1() */


#include "globals.h"
#include "extern.h"

short testTP1 ( void )
{
  PW_Start_Address = PW_i;

  /* size of the module */
  PW_WholeSampleSize = ( (in_data[PW_Start_Address+4]*256*256*256)+
			 (in_data[PW_Start_Address+5]*256*256)+
			 (in_data[PW_Start_Address+6]*256)+
			 in_data[PW_Start_Address+7] );
  if ( (PW_WholeSampleSize < 794) || (PW_WholeSampleSize > 2129178l) )
  {
    return BAD;
  }

  /* test finetunes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+32+PW_k*8] > 0x0f )
    {
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+33+PW_k*8] > 0x40 )
    {
      return BAD;
    }
  }

  /* sample data address */
  PW_l = ( (in_data[PW_Start_Address+28]*256*256*256)+
	   (in_data[PW_Start_Address+29]*256*256)+
	   (in_data[PW_Start_Address+30]*256)+
	   in_data[PW_Start_Address+31] );
  if ( (PW_l == 0) || (PW_l > PW_WholeSampleSize ) )
  {
    return BAD;
  }

  /* test sample sizes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+PW_k*8+34]*256)+in_data[PW_Start_Address+PW_k*8+35];
    PW_m = (in_data[PW_Start_Address+PW_k*8+36]*256)+in_data[PW_Start_Address+PW_k*8+37];
    PW_n = (in_data[PW_Start_Address+PW_k*8+38]*256)+in_data[PW_Start_Address+PW_k*8+39];
    PW_j *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( (PW_j > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m + PW_n) > (PW_j+2) )
    {
/*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m != 0) && (PW_n <= 2) )
    {
/*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* pattern list size */
  PW_l = in_data[PW_Start_Address+281];
  if ( (PW_l==0) || (PW_l>128) )
  {
    return BAD;
  }

  /* PW_WholeSampleSize is the size of the module :) */
  return GOOD;
}




void Rip_TP1 ( void )
{
  /* PW_WholeSampleSize is the size of the module :) */

  OutputSize = PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "Tracker Packer v1 module", TP1 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1); /* 0 could be enough */
}



/*
 *   TrackerPacker_v1.c   1998 (c) Asle / ReDoX
 *
 * Converts TP1 packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * Update : 1 may 2003
 * - changed way to locate pattern datas. Correct pattern list saved now.
 *
*/

void Depack_TP1 ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar poss[37][2];
  Uchar *Whatever;
  Uchar Note,Smp,Fx,FxVal;
  Uchar Patternlist[128];
  Uchar PatPos;
  long Pats_Address[128];
  long i=0,j=0,k;
  long Pats_Address_read[128];
  long Start_Pat_Address;
  long Whole_Sample_Size=0;
  long Sample_Data_Address;
  long Where=PW_Start_Address;
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  BZERO ( Pats_Address , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc (65536);
  BZERO ( Whatever , 65536 );
  fwrite ( &in_data[Where+8] , 20 , 1 , out );

  /* setting the first pattern address as the whole file size */
  Start_Pat_Address = 0xFFFFFF;

  Where += 28;
  /* sample data address */
  Sample_Data_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;
/*printf ( "sample data address : %ld\n" , Sample_Data_Address );*/

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    /* size */
    Whole_Sample_Size += (((in_data[Where+2]*256)+in_data[Where+3])*2);
    fwrite ( &in_data[Where+2] , 2 , 1 , out );

    /* write finetune,vol */
    fwrite ( &in_data[Where] , 2 , 1 , out );

    /* loops */
    fwrite ( &in_data[Where+4] , 4 , 1 , out );

    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Whole_Sample_Size );*/

  /* read size of pattern table */
  Where = PW_Start_Address + 281;
  PatPos = in_data[Where]+0x01;
  fwrite ( &PatPos , 1 , 1 , out );
  Where += 1;

  /* ntk byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  for ( i=0 ; i<PatPos ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    Where += 4;
    if ( Start_Pat_Address > Pats_Address[i] )
      Start_Pat_Address = Pats_Address[i];
    /*printf ( "[at:%ld]%3ld: %ld\n" , Where-4,i,Pats_Address[i] );*/
  }

  /*printf ( "Start_Pat_Address : %ld\n",Start_Pat_Address);*/

  /* setting real addresses */
  for ( i=0 ; i<PatPos ; i++ )
  {
    Pats_Address[i] -= (Start_Pat_Address - 794);
    /*printf ( "pats_Address[i] : %ld\n",Pats_Address[i] );*/
  }


  /*printf ( "address of the first pattern : %ld\n" , Start_Pat_Address );*/

  /* pattern datas */

  j=0;k=0;
  /*printf ( "converting pattern data " );*/
  for ( i=(PW_Start_Address+794) ; i<=(Sample_Data_Address+PW_Start_Address+1) ; i+=1,j+=4 )
  {
    if ( (j%1024) == 0 )
    {
      Pats_Address_read[k++] = i - PW_Start_Address;
      /*printf ( "addy[%2ld] : %x\n",k-1,Pats_Address_read[k-1]);*/
    }
    c1 = in_data[i];
    if ( c1 == 0xC0 )
    {
      continue;
    }

    if ( (c1&0xC0) == 0x80 )
    {
      c2 = in_data[i+1];
      Fx    = (c1>>2)&0x0f;
      FxVal = c2;
      Whatever[j+2]  = Fx;
      Whatever[j+3]  = FxVal;
      i += 1;
      continue;
    }
    c2 = in_data[i+1];
    c3 = in_data[i+2];

    Smp   = ((c2>>4)&0x0f) | ((c1<<4)&0x10);
    Note  = c1&0xFE;
    Fx    = c2&0x0F;
    FxVal = c3;

    Whatever[j] = Smp&0xf0;
    Whatever[j]   |= poss[(Note/2)][0];
    Whatever[j+1]  = poss[(Note/2)][1];
    Whatever[j+2]  = (Smp<<4)&0xf0;
    Whatever[j+2] |= Fx;
    Whatever[j+3]  = FxVal;
    i += 2;
  }
  k -= 1;
  Pats_Address_read[k] = 0;
  BZERO (Patternlist,128);
  for ( i=0 ; i<PatPos ; i++ )
    for ( j=0 ; j<k ; j++ )
      if ( Pats_Address[i] == Pats_Address_read[j] )
      {
	Patternlist[i] = (Uchar)j;
      }

  /* write pattern list */
  fwrite ( Patternlist , 128 , 1 , out );

  /* ID string */
  Patternlist[0] = 'M';
  Patternlist[1] = '.';
  Patternlist[2] = 'K';
  Patternlist[3] = '.';
  fwrite ( Patternlist , 4 , 1 , out );

  /* pattern data */
  fwrite ( Whatever, 1024*k, 1, out );
  free (Whatever);

  /* Sample data */
  Where = PW_Start_Address + Sample_Data_Address;
  /*printf ( "Where : %x\n",Where);*/
  fwrite ( &in_data[Where] , Whole_Sample_Size , 1 , out );

  Crap ( " Tracker Packer 1 " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
