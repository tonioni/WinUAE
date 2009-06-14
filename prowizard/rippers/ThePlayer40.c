/* testP40A() */
/* Rip_P40A() */
/* Rip_P40B() */
/* Depack_P40() */

#include "globals.h"
#include "extern.h"


short testP40A ( void )
{
  PW_Start_Address = PW_i;

  /* number of pattern (real) */
  PW_j = in_data[PW_Start_Address+4];
  if ( PW_j > 0x7f )
  {
    return BAD;
  }

  /* number of sample */
  PW_k = in_data[PW_Start_Address+6];
  if ( (PW_k > 0x1F) || (PW_k == 0) )
  {
    return BAD;
  }

  /* test volumes */
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+35+PW_l*16] > 0x40 )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_l=0 ; PW_l<PW_k ; PW_l++ )
  {
    /* size */
    PW_o = (in_data[PW_Start_Address+24+PW_l*16]*256)+in_data[PW_Start_Address+25+PW_l*16];
    /* loop size */
    PW_n = (in_data[PW_Start_Address+30+PW_l*16]*256)+in_data[PW_Start_Address+31+PW_l*16];
    PW_o *= 2;
    PW_n *= 2;

    if ( (PW_o > 0xFFFF) ||
	 (PW_n > 0xFFFF) )
    {
      return BAD;
    }

    if ( PW_n > (PW_o+2) )
    {
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
    PW_WholeSampleSize = 0;
    return BAD;
  }

  /* PW_WholeSampleSize is the size of the sample data .. WRONG !! */
  /* PW_k is the number of samples */
  return GOOD;
}



void Rip_P40A ( void )
{
  PW_l = ( (in_data[PW_Start_Address+16]*256*256*256) +
	   (in_data[PW_Start_Address+17]*256*256) +
	   (in_data[PW_Start_Address+18]*256) +
	   in_data[PW_Start_Address+19] );

  /* get whole sample size */
  PW_o = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_m = ( (in_data[PW_Start_Address+20+PW_j*16]*256*256*256) +
	     (in_data[PW_Start_Address+21+PW_j*16]*256*256) +
	     (in_data[PW_Start_Address+22+PW_j*16]*256) +
	     in_data[PW_Start_Address+23+PW_j*16] );
    if ( PW_m > PW_o )
    {
      PW_o = PW_m;
      PW_n = ( (in_data[PW_Start_Address+24+PW_j*16]*256) +
	       in_data[PW_Start_Address+25+PW_j*16] );
    }
  }

  OutputSize = PW_l + PW_o + (PW_n*2) + 4;

  CONVERT = GOOD;
  Save_Rip ( "The Player 4.0A module", P40A );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}



void Rip_P40B ( void )
{
  PW_l = ( (in_data[PW_Start_Address+16]*256*256*256) +
	   (in_data[PW_Start_Address+17]*256*256) +
	   (in_data[PW_Start_Address+18]*256) +
	   in_data[PW_Start_Address+19] );

  /* get whole sample size */
  PW_o = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_m = ( (in_data[PW_Start_Address+20+PW_j*16]*256*256*256) +
	     (in_data[PW_Start_Address+21+PW_j*16]*256*256) +
	     (in_data[PW_Start_Address+22+PW_j*16]*256) +
	     in_data[PW_Start_Address+23+PW_j*16] );
    if ( PW_m > PW_o )
    {
      PW_o = PW_m;
      PW_n = ( (in_data[PW_Start_Address+24+PW_j*16]*256) +
	       in_data[PW_Start_Address+25+PW_j*16] );
    }
  }

  OutputSize = PW_l + PW_o + (PW_n*2) + 4;

  CONVERT = GOOD;
  Save_Rip ( "The Player 4.0B module", P40B );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}



/*
 *   The_Player_4.0.c   1997 (c) Asle / ReDoX
 *
 * The Player 4.0a and 4.0b to Protracker.
 *
 * Note: It's a REAL mess !. It's VERY badly coded, I know. Just dont forget
 *      it was mainly done to test the description I made of P40* format. I
 *      certainly wont dare to beat Gryzor on the ground :). His Prowiz IS
 *      the converter to use !!!.
 *
 * Update: 28/11/99
 *     - removed fopen() and all attached functions.
 *     - overall speed and size optimizings.
 * Update : 26 nov 2003
 *     - used htonl() so that use of addy is now portable on 68k archs
*/
void Depack_P40 ( void )
{
  Uchar c1,c2,c3,c4,c5;
  Uchar *Whatever;
  Uchar PatPos = 0x00;
  Uchar Nbr_Sample = 0x00;
  Uchar poss[37][2];
  Uchar sample,note,Note[2];
  Uchar Track_Data[512][256];
  short Track_Addresses[128][4];
  long Track_Data_Address = 0;
  long Track_Table_Address = 0;
  long Sample_Data_Address = 0;
  long WholeSampleSize = 0;
  long SampleAddress[31];
  long SampleSize[31];
  long i=0,j,k,l,a,c,z;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Addresses , 128*4*2 );
  BZERO ( Track_Data , 512*256 );
  BZERO ( SampleAddress , 31*4 );
  BZERO ( SampleSize , 31*4 );

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read check ID */
  Where += 4;

  /* bypass Real number of pattern */
  Where += 1;

  /* read number of pattern in pattern list */
  PatPos = in_data[Where++];

  /* read number of samples */
  Nbr_Sample = in_data[Where++];

  /* bypass empty byte */
  Where += 1;


/**********/

  /* read track data address */
  Track_Data_Address = (in_data[Where]*256*256*256)+
                       (in_data[Where+1]*256*256)+
                       (in_data[Where+2]*256)+
                        in_data[Where+3];
  Where += 4;

  /* read track table address */
  Track_Table_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;

  /* read sample data address */
  Sample_Data_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;


  /* write title */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* sample headers stuff */
  for ( i=0 ; i<Nbr_Sample ; i++ )
  {
    /* read sample data address */
    j = (in_data[Where]*256*256*256)+
        (in_data[Where+1]*256*256)+
        (in_data[Where+2]*256)+
         in_data[Where+3];
    SampleAddress[i] = j;

    /* write sample name */
    fwrite ( Whatever , 22 , 1 , out );

    /* read sample size */
    SampleSize[i] = ((in_data[Where+4]*256)+in_data[Where+5])*2;
    WholeSampleSize += SampleSize[i];

    /* loop start */
    k = (in_data[Where+6]*256*256*256)+
        (in_data[Where+7]*256*256)+
        (in_data[Where+8]*256)+
         in_data[Where+9];

    /* writing now */
    fwrite ( &in_data[Where+4] , 2 , 1 , out );
    c1 = ((in_data[Where+12]*256)+in_data[Where+13])/74;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &in_data[Where+15] , 1 , 1 , out );
    k -= j;
    k /= 2;
    /* use of htonl() suggested by Xigh !.*/
    z = htonl(k);
    c1 = *((Uchar *)&z+2);
    c2 = *((Uchar *)&z+3);
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    fwrite ( &in_data[Where+10] , 2 , 1 , out );

    Where += 16;
  }

  /* go up to 31 samples */
  Whatever[29] = 0x01;
  while ( i != 31 )
  {
    fwrite ( Whatever , 30 , 1 , out );
    i += 1;
  }

  /* write size of pattern list */
  fwrite ( &PatPos , 1 , 1 , out );

  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* place file pointer at the pattern list address ... should be */
  /* useless, but then ... */
  Where = PW_Start_Address + Track_Table_Address + 4;

  /* create and write pattern list .. no optimization ! */
  /* I'll optimize when I'll feel in the mood */
  for ( c1=0x00 ; c1<PatPos ; c1++ )
  {
    fwrite ( &c1 , 1 , 1 , out );
  }
  c2 = 0x00;
  while ( c1<128 )
  {
    fwrite ( &c2 , 1 , 1 , out );
    c1 += 0x01;
  }

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* reading all the track addresses */
  for ( i=0 ; i<PatPos ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      Track_Addresses[i][j] = (in_data[Where]*256)+
                               in_data[Where+1]+
                               Track_Data_Address+4;
      Where += 2;
    }
  }


  /* rewrite the track data */
  /*printf ( "sorting and depacking tracks data ... " );*/
  for ( i=0 ; i<PatPos ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      Where = PW_Start_Address + Track_Addresses[i][j];
      for ( k=0 ; k<64 ; k++ )
      {
        c1 = in_data[Where++];
        c2 = in_data[Where++];
        c3 = in_data[Where++];
        c4 = in_data[Where++];

        if ( c1 != 0x80 )
        {

          sample = ((c1<<4)&0x10) | ((c2>>4)&0x0f);
          BZERO ( Note , 2 );
          note = c1 & 0x7f;
          Note[0] = poss[(note/2)][0];
          Note[1] = poss[(note/2)][1];
          switch ( c2&0x0f )
          {
            case 0x08:
              c2 -= 0x08;
              break;
            case 0x05:
            case 0x06:
            case 0x0A:
              c3 = (c3 > 0x7f) ? ((0x100-c3)<<4) : c3;
//              if ( c3 >= 0x80 )
//                c3 = (c3<<4)&0xf0;
              break;
            default:
              break;
          }
          Track_Data[i*4+j][k*4]   = (sample&0xf0) | (Note[0]&0x0f);
          Track_Data[i*4+j][k*4+1] = Note[1];
          Track_Data[i*4+j][k*4+2] = c2;
          Track_Data[i*4+j][k*4+3] = c3;

          if ( (c4 > 0x00) && (c4 <0x80) )
            k += c4;
          if ( (c4 > 0x7f) && (c4 <=0xff) )
          {
            k+=1;
            for ( l=256 ; l>c4 ; l-- )
            {
              Track_Data[i*4+j][k*4]   = (sample&0xf0) | (Note[0]&0x0f);
              Track_Data[i*4+j][k*4+1] = Note[1];
              Track_Data[i*4+j][k*4+2] = c2;
              Track_Data[i*4+j][k*4+3] = c3;
              k+=1;
            }
            k -= 1;
          }
        }

        else
        {
          a = Where;

          c5 = c2;
          Where = PW_Start_Address + (c3 * 256) + c4 + Track_Data_Address + 4;
/*fprintf ( debug , "%2d (pattern %ld)(at %x)\n" , c2 , i , a-4 );*/
          for ( c=0 ; c<=c5 ; c++ )
          {
/*fprintf ( debug , "%ld," , k );*/
            c1 = in_data[Where++];
            c2 = in_data[Where++];
            c3 = in_data[Where++];
            c4 = in_data[Where++];
            
            sample = ((c1<<4)&0x10) | ((c2>>4)&0x0f);
            BZERO ( Note , 2 );
            note = c1 & 0x7f;
            Note[0] = poss[(note/2)][0];
            Note[1] = poss[(note/2)][1];
            switch ( c2&0x0f )
            {
              case 0x08:
                c2 -= 0x08;
                break;
              case 0x05:
              case 0x06:
              case 0x0A:
              c3 = (c3 > 0x7f) ? ((0x100-c3)<<4) : c3;
//                if ( c3 >= 0x80 )
//                  c3 = (c3<<4)&0xf0;
                break;
              default:
                break;
            }
            Track_Data[i*4+j][k*4]   = (sample&0xf0) | (Note[0]&0x0f);
            Track_Data[i*4+j][k*4+1] = Note[1];
            Track_Data[i*4+j][k*4+2] = c2;
            Track_Data[i*4+j][k*4+3] = c3;

            if ( (c4 > 0x00) && (c4 <0x80) )
              k += c4;
            if ( (c4 > 0x7f) && (c4 <=0xff) )
            {
              k+=1;
              for ( l=256 ; l>c4 ; l-- )
              {
                Track_Data[i*4+j][k*4]   = (sample&0xf0) | (Note[0]&0x0f);
                Track_Data[i*4+j][k*4+1] = Note[1];
                Track_Data[i*4+j][k*4+2] = c2;
                Track_Data[i*4+j][k*4+3] = c3;
                k+=1;
              }
              k -= 1;
            }
            k += 1;
          }

          k -= 1;
          Where = a;
/*fprintf ( debug , "\n## back to %x\n" , a );*/
        }
      }
    }
  }
  /*printf ( "ok\n" );*/

/*
for ( i=0 ; i<PatPos*4 ; i++ )
{
  fprintf ( debug , "\n\ntrack #%ld----------------\n" , i );
  for ( j=0 ; j<64 ; j++ )
  {
    fprintf ( debug , "%2x %2x %2x %2x\n"
    , Track_Data[i][j*4]
    , Track_Data[i][j*4+1]
    , Track_Data[i][j*4+2]
    , Track_Data[i][j*4+3] );
  }
}
*/

  /* write pattern data */
  /*printf ( "writing pattern data ... " );*/
  /*fflush ( stdout );*/
  for ( i=0 ; i<PatPos ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<64 ; j++ )
    {
      for ( k=0 ; k<4 ; k++ )
      {
        Whatever[j*16+k*4]   = Track_Data[k+i*4][j*4];
        Whatever[j*16+k*4+1] = Track_Data[k+i*4][j*4+1];
        Whatever[j*16+k*4+2] = Track_Data[k+i*4][j*4+2];
        Whatever[j*16+k*4+3] = Track_Data[k+i*4][j*4+3];
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );
  /*printf ( "ok\n" );*/


  /* read and write sample data */
  /*printf ( "writing sample data ... " );*/
  for ( i=0 ; i<Nbr_Sample ; i++ )
  {
    Where = PW_Start_Address + SampleAddress[i]+Sample_Data_Address;
    fwrite ( &in_data[Where] , SampleSize[i] , 1 , out );
  }
  /*printf ( "ok\n" );*/

  if ( in_data[PW_Start_Address+3] == 'A' )
  {
    Crap ( " The Player 4.0A  " , BAD , BAD , out );
  }
  if ( in_data[PW_Start_Address+3] == 'B' )
  {
    Crap ( " The Player 4.0B  " , BAD , BAD , out );
  }

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
