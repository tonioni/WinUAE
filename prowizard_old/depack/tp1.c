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

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

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

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  BZERO ( Pats_Address , 128*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

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
