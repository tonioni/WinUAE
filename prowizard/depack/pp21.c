/*
 *   ProPacker_21.c   1997 (c) Asle / ReDoX
 *
 * Converts PP21 packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * Last revision : 26/11/1999 by Sylvain "Asle" Chipaux
 *                 reduced to only one FREAD.
 *                 Speed-up and Binary smaller.
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_PP21 ( void )
{
  Uchar c1=0x00,c2=0x00;
  short Max=0;
  Uchar Tracks_Numbers[4][128];
  short Tracks_PrePointers[512][64];
  Uchar NOP=0x00; /* number of pattern */
  Uchar *ReferenceTable;
  Uchar *WholeFile;
  Uchar *Whatever;
  long OverallCpt = 0;
  long i=0,j=0;
  long Total_Sample_Size=0;
  FILE *in,*out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Tracks_Numbers , 4*128 );
  BZERO ( Tracks_PrePointers , 512*128 );

  in = fopen ( (char *)OutName_final , "r+b" ); /* +b is safe bcoz OutName's just been saved */
  if (!in)
      return;
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* get input file size */
  fseek ( in , 0 , 2 );
  i = ftell ( in );
  fseek ( in , 0 , 0 );

  /* read but once input file */
  WholeFile = (Uchar *) malloc (i);
  BZERO ( WholeFile , i );
  fread ( WholeFile , i , 1 , in );
  fclose ( in );

  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );
    /* sample siz */
    Total_Sample_Size += (((WholeFile[OverallCpt]*256)+WholeFile[OverallCpt+1])*2);
    /* siz,fine,vol,lstart,lsize */
    fwrite ( &WholeFile[OverallCpt+0] , 8 , 1 , out );
    OverallCpt += 8;
  }

  /* pattern table lenght */
  NOP = WholeFile[OverallCpt];
  fwrite ( &NOP , 1 , 1 , out );
  OverallCpt += 1;

  /*printf ( "Number of patterns : %d\n" , NOP );*/

  /* NoiseTracker restart byte */
  fwrite ( &WholeFile[OverallCpt] , 1 , 1 , out );
  OverallCpt += 1;

  Max = 0;
  for ( j=0 ; j<4 ; j++ )
  {
    for ( i=0 ; i<128 ; i++ )
    {
      Tracks_Numbers[j][i] = WholeFile[OverallCpt];
      OverallCpt += 1;
      if ( Tracks_Numbers[j][i] > Max )
        Max = Tracks_Numbers[j][i];
    }
  }
  /*printf ( "Number of tracks : %d\n" , Max+1 );*/

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
      Tracks_PrePointers[j][i] = (WholeFile[OverallCpt]*256)+WholeFile[OverallCpt+1];
      OverallCpt += 2;
    }
  }

  /* read "reference table" size */
  j = ((WholeFile[OverallCpt]*256*256*256)+
       (WholeFile[OverallCpt+1]*256*256)+
       (WholeFile[OverallCpt+2]*256)+
        WholeFile[OverallCpt+3]);
  OverallCpt += 4;


  /* read "reference Table" */
  /*printf ( "Reference table location : %ld\n" , OverallCpt );*/
  ReferenceTable = (Uchar *) malloc ( j );
  for ( i=0 ; i<j ; i++,OverallCpt+=1 )
    ReferenceTable[i] = WholeFile[OverallCpt];

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
  fwrite ( &WholeFile[OverallCpt] , Total_Sample_Size , 1 , out );

  Crap ( "  ProPacker v2.1  " , BAD , BAD , out );
  /*
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[PP21 2 Ptk]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  free ( WholeFile );
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
