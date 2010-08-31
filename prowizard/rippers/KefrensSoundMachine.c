/* testKSM() */
/* Rip_KSM() */
/* Depack_KSM() */

#include "globals.h"
#include "extern.h"


short testKSM ( void )
{
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 1536) > PW_in_size)
    return BAD;

  /* test "a" */
  if ( in_data[PW_Start_Address+15] != 'a' )
    return BAD;

  /* test volumes */
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
    if ( in_data[PW_Start_Address+54+PW_k*32] > 0x40 )
      return BAD;

  /* test tracks data */
  /* first, get the highest track number .. */
  PW_j = 0;
  for ( PW_k=0 ; PW_k<1024 ; PW_k ++ )
  {
    if ( in_data[PW_Start_Address+PW_k+512] == 0xFF )
      break;
    if ( in_data[PW_Start_Address+PW_k+512] > PW_j )
      PW_j = in_data[PW_Start_Address+PW_k+512];
  }
  if ( PW_k == 1024 )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  if ( PW_j == 0 )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* so, now, PW_j is the highest track number (first is 00h !!) */
  /* real test on tracks data starts now */
  /* first, test if we don't get out of the file */
  if ( (PW_Start_Address + 1536 + PW_j*192 + 64*3) > PW_in_size )
    return BAD;
  /* now testing tracks */
  for ( PW_k = 0 ; PW_k <= PW_j ; PW_k++ )
    for ( PW_l=0 ; PW_l < 64 ; PW_l++ )
      if ( in_data[PW_Start_Address+1536+PW_k*192+PW_l*3] > 0x24 )
	return BAD;

  /* PW_j is still the highest track number */
  return GOOD;
}


void Rip_KSM ( void )
{
  /* PW_j is the highest track number */

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
    PW_WholeSampleSize += ((in_data[PW_Start_Address+52+PW_k*32]*256)+in_data[PW_Start_Address+53+PW_k*32]);
  
  OutputSize = ((PW_j+1)*192) + PW_WholeSampleSize + 1536;

  CONVERT = GOOD;
  Save_Rip ( "Kefrens Sound Machine module", KSM );
  
  if ( Save_Status == GOOD )
    PW_i += 2;  /* -1 should do but call it "just to be sure" :) */
}


/*
 *   Kefrens_Sound_Machine.c   1997 (c) Sylvain "Asle" Chipaux
 *
 * Depacks musics in the Kefrens Sound Machine format and saves in ptk.
 *
 * Last revision : 26/11/1999
 *      - reduced to only one FREAD.
 *      - Speed-up, Clean-up and Binary smaller.
 * Another Update : 28/11/1999
 *      - removed fopen() speed up and SIZE !.
 * Another Update : 05 may 2001
 *      - added transciption for sample names
 * Another Update : 26 nov 2003
 *      - used htonl() so that use of addy is now portable on 68k archs
 * update 30/08/10
 *      - patternlist with only one pattern fixed
 *      - conversion to STK instead of PTK
*/

#define ON  1
#define OFF 2

void Depack_KSM ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00,c5;
  Uchar Track_Numbers[128][4];
  Uchar Track_Numbers_Real[128][4];
  Uchar Track_Datas[4][192];
  Uchar Max=0x00;
  Uchar poss[37][2];
  Uchar PatPos;
  Uchar Status=ON;
  Uchar transco[]={'a','b','c','d','e','f','g','h','i','j'
                  ,'k','l','m','n','o','p','q','r','s','t'
                  ,'u','v','w','x','y','z'
                  ,'-',':','!','~','1','2','3','4','5','6'
                  ,'7','8','9','0',' ',';'};
  long Where=PW_Start_Address;
  long WholeSampleSize=0;
  unsigned long i=0,j=0,k=0,l;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Numbers , 128*4 );
  BZERO ( Track_Numbers_Real , 128*4 );

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( &in_data[Where+2] , 13 , 1 , out );
  fwrite ( Whatever , 7 , 1 , out );  /* fill-up there */

  /* read and write whole header */
  /*printf ( "Converting sample headers ... " );*/
  Where += 32;
  for ( i=0 ; i<15 ; i++ )
  {
    /* write name */
    for ( k=0 ; k<15 ; k++ )
      Whatever[230+k] = transco[in_data[Where+k]];
    fwrite ( &Whatever[230] , 22 , 1 , out );
    /* size */
    c1 = in_data[Where+20];
    c2 = in_data[Where+21];
    k = (in_data[Where+20] * 256) + in_data[Where+21];
    WholeSampleSize += k;
    c2 /= 2;
    if ( (c1/2)*2 != c1 )
    {
      if ( c2 < 0x80 )
        c2 += 0x80;
      else
      {
        c2 -= 0x80;
        c1 += 0x01;
      }
    }
    c1 /= 2;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    /* finetune */
    fwrite ( Whatever , 1 , 1 , out );
    /* volume */
    fwrite ( &in_data[Where+22] , 1 , 1 , out);
    /* loop start */
    c1 = in_data[Where+24];
    c2 = in_data[Where+25];
    j = k - ((c1*256)+c2);
    c2 /= 2;
    if ( (c1/2)*2 != c1 )
    {
      if ( c2 < 0x80 )
        c2 += 0x80;
      else
      {
        c2 -= 0x80;
        c1 += 0x01;
      }
    }
    c1 /= 2;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );

    if ( j != k )
    {
      /* write loop size */
      /* use of htonl() suggested by Xigh !.*/
      j/=2;
      l = htonl(j);
      c1 = *((Uchar *)&l+2);
      c2 = *((Uchar *)&l+3);
      fwrite ( &c1 , 1 , 1 , out );
      fwrite ( &c2 , 1 , 1 , out );
    }
    else
    {
      c1 = 0x00;
      c2 = 0x01;
      fwrite ( &c1 , 1 , 1 , out );
      fwrite ( &c2 , 1 , 1 , out );
    }
    Where += 32;
  }
  /*printf ( "ok\n" );*/

  /* pattern list */
  /*printf ( "creating the pattern list ... " );*/
  Where = PW_Start_Address+512;
  for ( PatPos=0x00 ; PatPos<128 ; PatPos++ )
  {
    Track_Numbers[PatPos][0] = in_data[Where+PatPos*4];
    Track_Numbers[PatPos][1] = in_data[Where+PatPos*4+1];
    Track_Numbers[PatPos][2] = in_data[Where+PatPos*4+2];
    Track_Numbers[PatPos][3] = in_data[Where+PatPos*4+3];
    if ( Track_Numbers[PatPos][0] == 0xFF )
      break;
    if ( Track_Numbers[PatPos][0] > Max )
      Max = Track_Numbers[PatPos][0];
    if ( Track_Numbers[PatPos][1] > Max )
      Max = Track_Numbers[PatPos][1];
    if ( Track_Numbers[PatPos][2] > Max )
      Max = Track_Numbers[PatPos][2];
    if ( Track_Numbers[PatPos][3] > Max )
      Max = Track_Numbers[PatPos][3];
  }

  /* write patpos */
  fwrite ( &PatPos , 1 , 1 , out );

  /* ntk byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* sort tracks numbers */
  c5 = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i == 0 )
    {
      Whatever[0] = c5;
      c5 += 0x01;
      continue;
    }
    for ( j=0 ; j<i ; j++ )
    {
      Status = ON;
      for ( k=0 ; k<4 ; k++ )
      {
        if ( Track_Numbers[j][k] != Track_Numbers[i][k] )
        {
          Status=OFF;
          break;
        }
      }
      if ( Status == ON )
      {
        Whatever[i] = Whatever[j];
        break;
      }
    }
    if ( Status == OFF )
    {
      Whatever[i] = c5;
      c5 += 0x01;
    }
    Status = ON;
  }
  /* c5 is the Max pattern number */

  /* create a real list of tracks numbers for the really existing patterns */
  c1 = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i==0 )
    {
      Track_Numbers_Real[c1][0] = Track_Numbers[i][0];
      Track_Numbers_Real[c1][1] = Track_Numbers[i][1];
      Track_Numbers_Real[c1][2] = Track_Numbers[i][2];
      Track_Numbers_Real[c1][3] = Track_Numbers[i][3];
      c1 += 0x01;
      continue;
    }
    for ( j=0 ; j<i ; j++ )
    {
      Status = ON;
      if ( Whatever[i] == Whatever[j] )
      {
        Status = OFF;
        break;
      }
    }
    if ( Status == OFF )
      continue;
    Track_Numbers_Real[c1][0] = Track_Numbers[i][0];
    Track_Numbers_Real[c1][1] = Track_Numbers[i][1];
    Track_Numbers_Real[c1][2] = Track_Numbers[i][2];
    Track_Numbers_Real[c1][3] = Track_Numbers[i][3];
    c1 += 0x01;
    Status = ON;
  }

  /* write pattern list */
  fwrite ( Whatever , 128 , 1 , out );
  /*printf ( "ok\n" );*/


  /* write ID */
  /*Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';*/
  /*fwrite ( Whatever , 4 , 1 , out );*/

  /* pattern data */
  /*printf ( "Converting pattern datas " );*/
  for ( i=0 ; i<c5 ; i++ )
  {
    BZERO ( Whatever , 1024 );
    BZERO ( Track_Datas , 192*4 );
    for ( k=0 ; k<4 ; k++ )
      for ( j=0 ; j<192 ; j++ )
        Track_Datas[k][j] = in_data[PW_Start_Address+1536+192*Track_Numbers_Real[i][k]+j];

    for ( j=0 ; j<64 ; j++ )
    {
      Whatever[j*16]    = poss[Track_Datas[0][j*3]][0];
      Whatever[j*16+1]  = poss[Track_Datas[0][j*3]][1];
      if ( (Track_Datas[0][j*3+1] & 0x0f) == 0x0D )
        Track_Datas[0][j*3+1] -= 0x03;
      Whatever[j*16+2]  = Track_Datas[0][j*3+1];
      Whatever[j*16+3]  = Track_Datas[0][j*3+2];

      Whatever[j*16+4]    = poss[Track_Datas[1][j*3]][0];
      Whatever[j*16+5]  = poss[Track_Datas[1][j*3]][1];
      if ( (Track_Datas[1][j*3+1] & 0x0f) == 0x0D )
        Track_Datas[1][j*3+1] -= 0x03;
      Whatever[j*16+6]  = Track_Datas[1][j*3+1];
      Whatever[j*16+7]  = Track_Datas[1][j*3+2];

      Whatever[j*16+8]    = poss[Track_Datas[2][j*3]][0];
      Whatever[j*16+9]  = poss[Track_Datas[2][j*3]][1];
      if ( (Track_Datas[2][j*3+1] & 0x0f) == 0x0D )
        Track_Datas[2][j*3+1] -= 0x03;
      Whatever[j*16+10] = Track_Datas[2][j*3+1];
      Whatever[j*16+11] = Track_Datas[2][j*3+2];

      Whatever[j*16+12]    = poss[Track_Datas[3][j*3]][0];
      Whatever[j*16+13]  = poss[Track_Datas[3][j*3]][1];
      if ( (Track_Datas[3][j*3+1] & 0x0f) == 0x0D )
        Track_Datas[3][j*3+1] -= 0x03;
      Whatever[j*16+14] = Track_Datas[3][j*3+1];
      Whatever[j*16+15] = Track_Datas[3][j*3+2];
    }


    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "." );*/
    /*fflush ( stdout );*/
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/
  /*fflush ( stdout );*/


  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  fwrite ( &in_data[PW_Start_Address+1536+(192*(Max+1))] , WholeSampleSize , 1 , out );


  /* crap */
  /*Crap ( "Kefrens SndMachine" , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
