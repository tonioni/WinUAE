/*  (3rd of april 2000)
 *    bugs pointed out by Thomas Neumann .. thx :) 
*/
/* testKRIS() */
/* Rip_KRIS() */
/* Depack_KRIS() */


#include "globals.h"
#include "extern.h"

short testKRIS ( void )
{
  /* test 1 */
  if ( (PW_i<952) || ((PW_Start_Address+977)>PW_in_size) )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-952;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* volume > 64 ? */
    if ( in_data[PW_Start_Address+47+PW_j*30] > 0x40 )
    {
      return BAD;
    }
    /* finetune > 15 ? */
    if ( in_data[PW_Start_Address+46+PW_j*30] > 0x0f )
    {
      return BAD;
    }
  }

  return GOOD;
}



/*
 * Update: 20/12/2000
 *  - debug .. correct size calculated now.
*/
void Rip_KRIS ( void )
{
  /* nothing was calculated in the test part */

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+44+PW_k*30]*256)+in_data[PW_Start_Address+45+PW_k*30])*2);
  /*printf ("\nKRIS:smpsiz:%ld\n", PW_WholeSampleSize);*/
  PW_l = 0;
  /*printf ("KRIS:");*/
  for ( PW_k=0 ; PW_k<512 ; PW_k++ )
  {
    /*printf ( "%2x-%2x ",in_data[PW_Start_Address+958+PW_k*2],in_data[PW_Start_Address+959+PW_k*2]);*/
    if ( (in_data[PW_Start_Address+958+PW_k*2]*256)+in_data[PW_Start_Address+959+PW_k*2] > PW_l )
      PW_l = (in_data[PW_Start_Address+958+PW_k*2]*256)+in_data[PW_Start_Address+959+PW_k*2];
  }
  /*printf ("\nKRIS:patsiz:%ld\n",PW_l);*/
  PW_k = 1984 + PW_l + 256;
  OutputSize = PW_k + PW_WholeSampleSize;
  
  CONVERT = GOOD;
  Save_Rip ( "KRIS Tracker module", KRIS_tracker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 954);  /* 953 should do but call it "just to be sure" :) */
}


/*
 *   Kris_packer.c   1997-2000 (c) Asle / ReDoX
 *
 * Kris Tracker to Protracker.
 *
 * Update: 28/11/1999
 *     - removed fopen()
 *     - Overall Speed and Size optimizings.
 * Update: 03/04/2000
 *     - no more sample beginning with $01 :) (Thomas Neumann again ... thx)
 * Update: 20/12/2000
 *     - major debugging around the patterntable ...
 * Update: 02/10/2005
 *     - testing fopen()
*/

void Depack_KRIS ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00;
  Uchar poss[37][2];
  Uchar Max=0x00;
  Uchar Note,Smp,Fx,FxVal;
  Uchar TrackData[512][256];
  Uchar PatternTableSize=0x00;
  Uchar PatternTable[128];
  short TrackAddressTable[128][4];
  long i=0,j=0,k=0;
  long WholeSampleSize=0;
  long Where = PW_Start_Address;
  short MaxTrackAddress=0;
  FILE *out; /*,*debug;*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  BZERO ( TrackAddressTable , 128*4*2 );
  BZERO ( TrackData , 512*256 );
  BZERO ( PatternTable , 128 );

/*  debug = fopen ( "debug" , "w+b" );*/

  /* title */
  fwrite ( &in_data[Where] , 20 , 1 , out );
  Where += 22;  /* 20 + 2 */

  /* 31 samples */
  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever , 1024);
  for ( i=0 ; i<31 ; i++ )
  {
    /* sample name,siz,fine,vol */
    if ( in_data[Where] == 0x01 )
    {
      fwrite ( Whatever , 20 , 1 , out );
      fwrite ( &in_data[Where+20] , 6 , 1 , out );
    }
    else
      fwrite ( &in_data[Where] , 26 , 1 , out );

    /* size */
    WholeSampleSize += (((in_data[Where+22]*256)+in_data[Where+23])*2);

    /* loop start */
    c1 = in_data[Where+26]/2;
    c2 = in_data[Where+27]/2;
    if ( (c1*2) != in_data[Where+26] )
      c2 += 1;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );

    /* loop size */
    fwrite ( &in_data[Where+28] , 2 , 1 , out );

    Where += 30;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* bypass ID "KRIS" */
  Where += 4;

  /* number of pattern in pattern list */
  /* Noisetracker restart byte */
  PatternTableSize = in_data[Where];
  fwrite ( &in_data[Where] , 2 , 1 , out );
  Where += 2;

  /* pattern table (read,count and write) */
  c1 = 0x00;
  k=0;
  for ( i=0 ; i<128 ; i++ , k++ )
  {
/*fprintf ( debug , "%-2ld" , i );*/
    for ( j=0 ; j<4 ; j++ )
    {
      TrackAddressTable[k][j]= (in_data[Where]*256)+in_data[Where+1];
      if ( TrackAddressTable[k][j] > MaxTrackAddress )
        MaxTrackAddress = TrackAddressTable[k][j];
      Where += 2;
/*fprintf ( debug , "- %4d" , TrackAddressTable[k][j] );*/
    }
/*fprintf ( debug , "\n" );*/
    for ( j=0 ; j<k ; j++ )
    {
/*fprintf ( debug , "- %2ld - %4d - %4d - %4d - %4d"
                , j
                , TrackAddressTable[j][0]
                , TrackAddressTable[j][1]
                , TrackAddressTable[j][2]
                , TrackAddressTable[j][3] );*/
      if ( (TrackAddressTable[j][0] == TrackAddressTable[k][0]) &&
           (TrackAddressTable[j][1] == TrackAddressTable[k][1]) &&
           (TrackAddressTable[j][2] == TrackAddressTable[k][2]) &&
           (TrackAddressTable[j][3] == TrackAddressTable[k][3]) )
      {
/*fprintf ( debug , " --- ok\n" );*/
        PatternTable[i] = j;
/*fprintf ( debug , "---> patterntable[%ld] : %ld\n" , i , PatternTable[i] );*/
        k-=1;
        j = 9999l;  /* hum ... sure now ! */
        break;
      }
/*fprintf ( debug , "\n" );*/
    }
    if ( k == j )
    {
      PatternTable[i] = c1;
      c1 += 0x01;
/*fprintf ( debug , "---> patterntable[%ld] : %d\n" , i , PatternTable[i] );*/
    }
    fwrite ( &PatternTable[i] , 1 , 1 , out );
  }

  Max = c1;
  /*printf ( "Number of patterns : %d\n" , Max );*/

  /* ptk ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* bypass two unknown bytes */
  Where += 2;

  /* Track data ... */
  for ( i=0 ; i<=(MaxTrackAddress/256) ; i+=1 )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<256 ; j++ )
      Whatever[j] = in_data[Where+j];
    Where += 256;

    for ( j=0 ; j<64 ; j++ )
    {
      Note  = Whatever[j*4];
      Smp   = Whatever[j*4+1];
      Fx    = Whatever[j*4+2] & 0x0f;
      FxVal = Whatever[j*4+3];

      TrackData[i][j*4] = (Smp & 0xf0);
      /*if ( (Note < 0x46) || (Note > 0xa8) )*/
        /*printf ( "!! Note value : %x  (beside ptk 3 octaves limit)\n" , Note );*/

      if ( Note != 0xa8 )
      {
        TrackData[i][j*4]   |= poss[(Note/2)-35][0];
        TrackData[i][j*4+1] |= poss[(Note/2)-35][1];
      }
      TrackData[i][j*4+2]  = (Smp<<4)&0xf0;
      TrackData[i][j*4+2] |= (Fx & 0x0f);
      TrackData[i][j*4+3] = FxVal;
    }
  }

  for ( i=0 ; i<Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<64 ; j++ )
    {
      Whatever[j*16]   = TrackData[TrackAddressTable[i][0]/256][j*4];
      Whatever[j*16+1] = TrackData[TrackAddressTable[i][0]/256][j*4+1];
      Whatever[j*16+2] = TrackData[TrackAddressTable[i][0]/256][j*4+2];
      Whatever[j*16+3] = TrackData[TrackAddressTable[i][0]/256][j*4+3];

      Whatever[j*16+4] = TrackData[TrackAddressTable[i][1]/256][j*4];
      Whatever[j*16+5] = TrackData[TrackAddressTable[i][1]/256][j*4+1];
      Whatever[j*16+6] = TrackData[TrackAddressTable[i][1]/256][j*4+2];
      Whatever[j*16+7] = TrackData[TrackAddressTable[i][1]/256][j*4+3];

      Whatever[j*16+8] = TrackData[TrackAddressTable[i][2]/256][j*4];
      Whatever[j*16+9] = TrackData[TrackAddressTable[i][2]/256][j*4+1];
      Whatever[j*16+10]= TrackData[TrackAddressTable[i][2]/256][j*4+2];
      Whatever[j*16+11]= TrackData[TrackAddressTable[i][2]/256][j*4+3];

      Whatever[j*16+12]= TrackData[TrackAddressTable[i][3]/256][j*4];
      Whatever[j*16+13]= TrackData[TrackAddressTable[i][3]/256][j*4+1];
      Whatever[j*16+14]= TrackData[TrackAddressTable[i][3]/256][j*4+2];
      Whatever[j*16+15]= TrackData[TrackAddressTable[i][3]/256][j*4+3];

    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  Crap ( "   Kris tracker   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
/*  fclose ( debug );*/

  printf ( "done\n" );
  return; /* useless ... but */
}

