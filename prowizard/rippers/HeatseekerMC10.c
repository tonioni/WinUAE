/*  (May 2002)
 *    added test_smps()
*/
/* testHEATSEEKER() */
/* Rip_HEATSEEKER() */
/* Depack_HEATSEEKER() */


#include "globals.h"
#include "extern.h"


short testHEATSEEKER ( void )
{
  int nbr_notes=0;

  if ( (PW_i < 3) || ((PW_i+375)>=PW_in_size))
  {
    return BAD;
  }
  PW_Start_Address = PW_i-3;

  /* size of the pattern table */
  if ( (in_data[PW_Start_Address+248] > 0x7f) ||
       (in_data[PW_Start_Address+248] == 0x00) )
  {
    return BAD;
  }

  /* test noisetracker byte */
  if ( in_data[PW_Start_Address+249] != 0x7f )
  {
    return BAD;
  }

  /* test samples */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (in_data[PW_Start_Address+PW_k*8]*256)+in_data[PW_Start_Address+1+PW_k*8];
    /* loop start */
    PW_m = (in_data[PW_Start_Address+PW_k*8+4]*256)+in_data[PW_Start_Address+5+PW_k*8];
    /* loop size */
    PW_n = (in_data[PW_Start_Address+PW_k*8+6]*256)+in_data[PW_Start_Address+7+PW_k*8];
    PW_j *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( test_smps(PW_j, PW_m, PW_n, in_data[PW_Start_Address+3+PW_k*8], in_data[PW_Start_Address+2+PW_k*8] ) == BAD )
      return BAD;
    if ( (PW_j > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern table */
  PW_l = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j ++ )
  {
    if ( in_data[PW_Start_Address+250+PW_j] > 0x7f )
    {
      return BAD;
    }
    if ( in_data[PW_Start_Address+250+PW_j] > PW_l )
      PW_l = in_data[PW_Start_Address+250+PW_j];
  }
  /* PW_l = highest pattern number */
  if ( (PW_Start_Address + (PW_l*16) + 378) > PW_in_size )
  {
    /* PW_l*16 is the minimum size of all patterns */
    return BAD;
  }

  /* test notes */
  PW_k=0;
  PW_j=0;
  for ( PW_m=0 ; PW_m<=PW_l ; PW_m++ )
  {
    for ( PW_n=0 ; PW_n<4 ; PW_n++ )
    {
      for ( PW_o=0 ; PW_o<64 ; PW_o++ )
      {
	switch (in_data[PW_Start_Address+378+PW_j]&0xE0)
	{
	  case 0x00:
	    if ( ((in_data[PW_Start_Address+378+PW_j]&0x0F)>0x03) || (((in_data[PW_Start_Address+378+PW_j]&0x0F)==0x00) && (in_data[PW_Start_Address+379+PW_j]<0x71) && (in_data[PW_Start_Address+379+PW_j]!=0x00)))
	    {
	      /*printf ( "#6) start:%ld (%x) (at:%x) (PW_l:%ld)\n",PW_Start_Address, in_data[PW_Start_Address+378+PW_j], PW_Start_Address+378+PW_j,PW_l );*/
	      return BAD;
	    }
	    if ( ((in_data[PW_Start_Address+380+PW_j]&0x0f) == 0x0d) && (in_data[PW_Start_Address+381+PW_j]>0x64))
	    {
	      /*	      printf ( "#6,5 Start:%ld cmd D arg : %x\n", PW_Start_Address, in_data[PW_Start_Address+381+PW_j] );*/
	      return BAD;
	    }
	    PW_k += 4;
	    PW_j += 4;
	    if ( ((in_data[PW_Start_Address+378+PW_j]&0x0f)!=0x00) || (in_data[PW_Start_Address+379+PW_j]!=0x00))
	      nbr_notes = 1;
	    break;
	  case 0x80:
	    if (( in_data[PW_Start_Address+379+PW_j]!=0x00 ) || ( in_data[PW_Start_Address+380+PW_j]!=0x00 ))
	    {
/*printf ( "#7) start:%ld (%x) (at:%x)\n"
,PW_Start_Address, in_data[PW_Start_Address+379+PW_j], PW_Start_Address+379+PW_j );*/
	      return BAD;
	    }
	    PW_o += in_data[PW_Start_Address+381+PW_j];
	    PW_j += 4;
	    PW_k += 4;
	    break;
	  case 0xC0:
	    if ( in_data[PW_Start_Address+379+PW_j]!=0x00 )
	    {
/*printf ( "#7) start:%ld (%x) (at:%x)\n"
,PW_Start_Address, in_data[PW_Start_Address+379+PW_j], PW_Start_Address+379+PW_j );*/
	      return BAD;
	    }
	    PW_o = 100;
	    PW_j += 4;
	    PW_k += 4;
	    break;
	  default:
	    return BAD;
	    break;
	}
      }
    }
  }
  if ( nbr_notes == 0 )
  {
    /* only empty notes */
    return BAD;
  }

  /* PW_k is the size of the pattern data */
  /* PW_WholeSampleSize is the size of the sample data */
  return GOOD;
}


void Rip_HEATSEEKER ( void )
{
  OutputSize = PW_k + PW_WholeSampleSize + 378;
  /*  printf ( "\b\b\b\b\b\b\b\bHeatseeker module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/

  CONVERT = GOOD;
  Save_Rip ( "Heatseeker module", Heatseeker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}


/*
 *   Heatseeker_mc1.0.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk Heatseeker packed MODs
 *
 * Note: There's a good job ! .. gosh !.
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

void Depack_HEATSEEKER ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar Pat_Max=0x00;
  Uchar *Whatever;
  long Track_Addresses[512];
  long i=0,j=0,k=0,l=0,m;
  long WholeSampleSize=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Addresses , 512*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 6 , 1 , out );
    Whatever[32] = in_data[Where+6];
    Whatever[33] = in_data[Where+7];
    if ( (Whatever[32] == 0x00) && (Whatever[33] == 0x00) )
      Whatever[33] = 0x01;
    fwrite ( &Whatever[32] , 2 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* read and write pattern table lenght */
  /* read and write NoiseTracker byte */
  fwrite ( &in_data[Where] , 2 , 1 , out );
  Where += 2;

  /* read and write pattern list and get highest patt number */
  for ( i=0 ; i<128 ; i++ )
  {
    Whatever[i] = in_data[Where++];
    if ( Whatever[i] > Pat_Max )
      Pat_Max = Whatever[i];
  }
  fwrite ( Whatever , 128 , 1 , out );
  Pat_Max += 1;
  /*printf ( "Number of pattern : %d\n" , Pat_Max );*/

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  for ( i=0 ; i<Pat_Max ; i++ )
  {
/*fprintf ( info , "\n\n\nPattern %ld :\n" , i );*/
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      Track_Addresses[i*4+j] = Where;
/*fprintf ( info , "Voice %ld (at:%ld):\n" , j , Track_Addresses[i*4+j]);*/
      for ( k=0 ; k<64 ; k++ )
      {
        c1 = in_data[Where++];
/*fprintf ( info , "%2ld: %2x , " , k , c1 );*/
        if ( c1 == 0x80 )
        {
          c2 = in_data[Where++];
          c3 = in_data[Where++];
          c4 = in_data[Where++];
/*fprintf ( info , "%2x , %2x , %2x !!! (%ld)\n" , c2 , c3 , c4 ,Where );*/
          k += c4;
          continue;
        }
        if ( c1 == 0xc0 )
        {
          c2 = in_data[Where++];
          c3 = in_data[Where++];
          c4 = in_data[Where++];
          l = Where;
          Where = Track_Addresses[((c3*256)+c4)/4];
/*fprintf ( info , "now at %ld (voice : %d)\n" , ftell ( in ) , ((c3*256)+c4)/4 );*/
          for ( m=0 ; m<64 ; m++ )
          {
            c1 = in_data[Where++];
/*fprintf ( info , "%2ld: %2x , " , k , c1 );*/
            if ( c1 == 0x80 )
            {
              c2 = in_data[Where++];
              c3 = in_data[Where++];
              c4 = in_data[Where++];
/*fprintf ( info , "%2x , %2x , %2x !!! (%ld)\n" , c2 , c3 , c4 ,Where);*/
              m += c4;
              continue;
            }
            c2 = in_data[Where++];
            c3 = in_data[Where++];
            c4 = in_data[Where++];
/*fprintf ( info , "%2x , %2x , %2x  (%ld)\n" , c2 , c3 , c4 ,ftell (in));*/
            Whatever[m*16+j*4]   = c1;
            Whatever[m*16+j*4+1] = c2;
            Whatever[m*16+j*4+2] = c3;
            Whatever[m*16+j*4+3] = c4;
          }
/*fprintf ( info , "%2x , %2x , %2x ??? (%ld)\n" , c2 , c3 , c4 ,ftell (in ));*/
          Where = l;
          k += 100;
          continue;
        }
        c2 = in_data[Where++];
        c3 = in_data[Where++];
        c4 = in_data[Where++];
/*fprintf ( info , "%2x , %2x , %2x  (%ld)\n" , c2 , c3 , c4 ,ftell (in));*/
        Whatever[k*16+j*4]   = c1;
        Whatever[k*16+j*4+1] = c2;
        Whatever[k*16+j*4+2] = c3;
        Whatever[k*16+j*4+3] = c4;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
    /*fflush ( stdout );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/

  /* sample data */
/*printf ( "where : %ld  (wholesamplesize : %ld)\n" , ftell ( in ) , WholeSampleSize );*/
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( " Heatseeker mc1.0 " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
