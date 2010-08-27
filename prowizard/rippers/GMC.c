/* testGMC() */
/* Rip_GMC() */
/* Depack_GMC() */

#include "globals.h"
#include "extern.h"


short testGMC ( void )
{
  /* test #1 */
  if ( (PW_i<7) || ((PW_Start_Address+444)>PW_in_size) )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-7;

  /* samples descriptions */
  PW_WholeSampleSize=0;
  PW_j=0;
  for ( PW_k = 0 ; PW_k < 15 ; PW_k ++ )
  {
    PW_o = (in_data[PW_Start_Address+16*PW_k+4]*256)+in_data[PW_Start_Address+16*PW_k+5];
    PW_n = (in_data[PW_Start_Address+16*PW_k+12]*256)+in_data[PW_Start_Address+16*PW_k+13];
    PW_o *= 2;
    /* volumes */
    if ( in_data[PW_Start_Address + 7 + (16*PW_k)] > 0x40 )
    {
/*printf ( "#2\n" );*/
      return BAD;
    }
    /* size */
    if ( PW_o > 0xFFFF )
    {
/*printf ( "#2,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_n > PW_o )
    {
/*printf ( "#2,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_o;
    if ( PW_o != 0 )
      PW_j = PW_k+1;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
/*printf ( "#2,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the highest not null sample */

  /* pattern table size */
  if ( ( in_data[PW_Start_Address+243] > 0x64 ) ||
       ( in_data[PW_Start_Address+243] == 0x00 ) )
  {
    return BAD;
  }

  /* pattern order table */
  PW_l=0;
  for ( PW_n=0 ; PW_n<100 ; PW_n++ )
  {
    PW_k = ((in_data[PW_Start_Address+244+PW_n*2]*256)+
	    in_data[PW_Start_Address+245+PW_n*2]);
    if ( ((PW_k/1024)*1024) != PW_k )
    {
/*printf ( "#4 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
      return BAD;
    }
    PW_l = ((PW_k/1024)>PW_l) ? PW_k/1024 : PW_l;
  }
  PW_l += 1;
  /* PW_l is the number of pattern */
  /* 20100822 - was wrong when there was only one pattern */
  /*if ( (PW_l == 1) || (PW_l >0x64) )*/
  if ( PW_l >0x64 )
  {
/*printf ("#4.5 Start:%ld\n",PW_Start_Address);*/
    return BAD;
  }

  /* test pattern data */
  PW_o = in_data[PW_Start_Address+243];
  PW_m = 0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    for ( PW_n=0 ; PW_n<256 ; PW_n++ )
    {
      if ( ( in_data[PW_Start_Address+444+PW_k*1024+PW_n*4] > 0x03 ) ||
	   ( (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) >= 0x90 ))
      {
/*printf ( "#5,0 Start:%ld (PW_k:%ld)\n" , PW_Start_Address , PW_k);*/
	return BAD;
      }
      /* 20100822 - following test is removed as there seem to exist GMC with 
         sample number higher than the actual number of sample saved ... 
         seen in Jumping Jack Son game*/
/*      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) > PW_j )
      {
printf ( "#5,1 Start:%ld (PW_j:%ld) (where:%ld) (value:%x)\n"
         , PW_Start_Address , PW_j , PW_Start_Address+444+PW_k*1024+PW_n*4+2
         , ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0xf0)>>4) );
	return BAD;
      }*/
      /* test volume effect if value is > 64 */
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 3) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x64) )
      {
/*printf ( "#5,2 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 4) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > 0x63) )
      {
/*printf ( "#5,3 Start:%ld (PW_j:%ld)\n" , PW_Start_Address , PW_j);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 5) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] > PW_o+1) )
      {
/*printf ( "#5,4 Start:%ld (effect:5)(PW_o:%ld)(4th note byte:%x)\n" , PW_Start_Address , PW_j , in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3]);*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 6) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,5 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+2]&0x0f) == 7) &&
           (in_data[PW_Start_Address+444+PW_k*1024+PW_n*4+3] >= 0x02) )
      {
/*printf ( "#5,6 Start:%ld (at:%ld)\n" , PW_Start_Address , PW_Start_Address+444+PW_k*1024+PW_n*4+3 );*/
	return BAD;
      }
      if ( ((in_data[PW_Start_Address+444+PW_k*1024+PW_n*4]&0x0f) > 0x00) || (in_data[PW_Start_Address+445+PW_k*1024+PW_n*4] > 0x00) )
	PW_m = 1;
    }
  }
  if ( PW_m == 0 )
  {
    /* only empty notes */
    return BAD;
  }
  /* PW_WholeSampleSize is the whole sample size */

  return GOOD;
}


void Rip_GMC ( void )
{
  /* PW_l is still the number of pattern to play */
  /* PW_WholeSampleSize is already the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_l*1024) + 444;

  CONVERT = GOOD;
  Save_Rip ( "Game Music Creator module", GMC );
  
  if ( Save_Status == GOOD )
    PW_i += 444; /* after header */
}

/*
 *   Game_Music_Creator.c   1997 (c) Sylvain "Asle" Chipaux
 *
 * Depacks musics in the Game Music Creator format and saves in ptk.
 *
 * update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
 *
 * update: 23/08/10
 *   - clean up
*/


void Depack_GMC ( void )
{
  Uchar *Whatever;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc ( 1084 );
  BZERO ( Whatever , 1084 );

  /* read and write whole header */
  /*printf ( "Converting sample headers ... " );*/
  for ( i=0 ; i<15 ; i++ )
  {
    Whatever[(i*30)+42] = in_data[Where+4];
    Whatever[(i*30)+43] = in_data[Where+5];
    WholeSampleSize += (((in_data[Where+4]*256)+in_data[Where+5])*2);
    Whatever[(i*30)+44] = in_data[Where+6];
    Whatever[(i*30)+45] = in_data[Where+7];
    Whatever[(i*30)+48] = in_data[Where+12];
    Whatever[(i*30)+49] = in_data[Where+13];

    if ( (Whatever[(i*30)+48] == 0x00) && (Whatever[(i*30)+49] == 0x02) )
      Whatever[(i*30)+49] = 0x01;

    /* loop start stuff - must check if there's a loop */
    if ( ((in_data[Where+12]*256)+in_data[Where+13]) > 2 )
    {
      /* ok, there's a loop size. use addresses to get the loop start */
      /* I know, that could be done some other way ... */
      unsigned char c=0x00,d;
      if ( in_data[Where+3] > in_data[Where+11] )
      {d = in_data[Where+11]+256 - in_data[Where+3]; c += 1;}
      else
        d = in_data[Where+11] - in_data[Where+3];
      if ( in_data[Where+2] > in_data[Where+10] )
        c += (in_data[Where+10]+256 - in_data[Where+2]);
      else
        c += in_data[Where+10] - in_data[Where+2];
      /* other bytes _must_ be 0 */
      Whatever[(i*30)+46] = c/2;
      Whatever[(i*30)+47] = d/2;
    }

    Where += 16;
  }

  /* pattern list size */
  Where = PW_Start_Address + 0xF3;
  Whatever[950] = in_data[Where++];
  Whatever[951] = 0x7F;

  /* read and write size of pattern list */
  /*printf ( "Creating the pattern table ... " );*/
  Max = 0x00;
  for ( i=0 ; i<100 ; i++ )
  {
    Whatever[952+i] = ((in_data[Where]*256)+in_data[Where+1])/1024;
    Where += 2;
    if ( Whatever[952+i] > Max )
      Max = Whatever[952+i];
  }

  /* write ID */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';
  fwrite ( Whatever , 1084 , 1 , out );


  /* pattern data */
  /*printf ( "Converting pattern datas " );*/
  Where = PW_Start_Address + 444;
  for ( i=0 ; i<=Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<1024 ; j++ ) Whatever[j] = in_data[Where++];
    for ( j=0 ; j<256 ; j++ )
    {
      switch ( Whatever[(j*4)+2]&0x0f )
      {
        case 3: /* replace by C */
          Whatever[(j*4)+2] += 0x09;
          break;
        case 4: /* replace by D */
          Whatever[(j*4)+2] += 0x09;
          break;
        case 5: /* replace by B */
          Whatever[(j*4)+2] += 0x06;
          break;
        case 6: /* replace by E0 */
          Whatever[(j*4)+2] += 0x08;
          break;
        case 7: /* replace by E0 */
          Whatever[(j*4)+2] += 0x07;
          break;
        case 8: /* replace by F */
          Whatever[(j*4)+2] += 0x07;
          break;
        default:
          break;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );

  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "Game Music Creator" , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
