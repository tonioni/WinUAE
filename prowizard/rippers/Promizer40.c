/* testPM40() */
/* Rip_PM40() */
/* Depack_PM40() */

#include "globals.h"
#include "extern.h"


int16_t	 testPM40 ( void )
{
  PW_Start_Address = PW_i;

  /* size of the pattern list */
  PW_j = in_data[PW_Start_Address+7];
  if ( PW_j > 0x7f )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the size of the pattern list */

  /* finetune */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+PW_k*8+266] > 0x0f )
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  
  /* volume */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+PW_k*8+267] > 0x40 )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  
  /* sample data address */
  PW_l = ( (in_data[PW_Start_Address+512]*256*256*256)+
	   (in_data[PW_Start_Address+513]*256*256)+
	   (in_data[PW_Start_Address+514]*256)+
	   in_data[PW_Start_Address+515] );
  if ( (PW_l <= 520) || (PW_l > 2500000l ) )
  {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_l is the sample data address */
  return GOOD;
}



void Rip_PM40 ( void )
{
  /* PW_l is the sample data address */

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+264+PW_k*8]*256)+in_data[PW_Start_Address+265+PW_k*8])*2);

  OutputSize = PW_WholeSampleSize + PW_l + 4;

  CONVERT = GOOD;
  Save_Rip ( "Promizer 4.0 module", PM40 );
  
  if ( Save_Status == GOOD )
    PW_i += 2;  /* 1 should do but call it "just to be sure" :) */
}




/*
 *   Promizer_40.c   1997 (c) Asle / ReDoX
 *
 * Converts PM40 packed MODs back to PTK MODs
 *
 * 20100112 - complete rewrite
 *
*/

#define ON  0
#define OFF 1
#define SAMPLE_DESC         264
#define ADDRESS_SAMPLE_DATA 512
#define ADDRESS_REF_TABLE   516
#define PATTERN_DATA        520

void Depack_PM40 ( void )
{
  int32_t	 Pats_Address[128];
  int32_t	 Read_Pats_Address[128];
  uint8_t *Whatever;
  uint8_t c1=0x00,c2=0x00;
  int32_t	 Where;
  uint8_t *Pattern;
  int32_t	 i=0,j=0,k=0,l=0,m=0;
  int32_t	 Total_Sample_Size=0;
  int32_t	 PatDataSize=0l;
  int32_t	 SmpAddy;
  uint8_t FLAG=OFF;
  uint8_t poss[37][2];
  FILE *out;
  /*FILE *info;
  info = fopen("info.txt","w+b");*/

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Pattern = (uint8_t *) malloc (65536);
  BZERO ( Pattern , 65536 );
  BZERO ( Pats_Address , 128*4 );

  /* title (empty) */
  Whatever = (uint8_t *)malloc(1085);
  BZERO (Whatever,1085);

  /* read and write sample headers */
  /*printf ( "Converting sample headers ... " );*/
  Where = PW_Start_Address + SAMPLE_DESC;
  for ( i=0 ; i<31 ; i++ )
  {
    Whatever[42+30*i] = in_data[Where];
    Whatever[43+30*i] = in_data[Where+1];
    Whatever[44+30*i] = in_data[Where+2];
    Whatever[45+30*i] = in_data[Where+3];
    Whatever[46+30*i] = in_data[Where+4];
    Whatever[47+30*i] = in_data[Where+5];
    Whatever[48+30*i] = in_data[Where+6];
    Whatever[49+30*i] = in_data[Where+7];
    Total_Sample_Size += (((Whatever[42+30*i]*256)+Whatever[43+30*i])*2);
    Where += 8;
  }
  /*printf ( "ok\n" );*/

  /* size of the pattern list */
  Whatever[950] = in_data[PW_Start_Address+7];

  /* NoiseTracker restart byte */
  Whatever[951] = 0x7f;


  /* pattern addresses */
  Where = PW_Start_Address + 8;
  for ( i=0 ; i<Whatever[950] ; i++ )
  {
    Pats_Address[i] = (in_data[Where+i*2]*256)+in_data[Where+i*2+1];
    /*printf ("%ld + ",Pats_Address[i]);*/
  }

  Where = PW_Start_Address + ADDRESS_SAMPLE_DATA;
  SmpAddy = (in_data[Where]*256*256*256 +
      in_data[Where+1]*256*256 +
      in_data[Where+2]*256 +
      in_data[Where+3]) + 4;
  
  Where += 4;
  l = (in_data[Where]*256*256*256 +
      in_data[Where+1]*256*256 +
      in_data[Where+2]*256 +
      in_data[Where+3]) + 8;

  Where += 4;
  PatDataSize = l - PATTERN_DATA;
  /*printf ("PatDataSize : %ld\n",PatDataSize);*/

  /* convert pattern data here */
  i=0; k=0;
  c1 = 0x00;
  for (j=0; j<PatDataSize; j+=2)
  {
    if ( ((i%1024) == 0 ) || (i == 0))
    {
      Read_Pats_Address[c1] = j;
      /*fprintf ( info, " -> new pattern %2d (addy :%ld)\n", c1, j );*/
      /*printf ("%lx(%d) - ",j,c1);*/
      fflush (stdout);
      c1 += 0x01;
    }

    m = (in_data[PW_Start_Address+j+PATTERN_DATA]*256)+in_data[PW_Start_Address+j+PATTERN_DATA+1];
    if (m == 0) /* no note */
    {
      /*fprintf (info,"[%4x][%3ld][%ld]: no note", j,i,k%4);*/
    }
    else
    {
      m -= 1;
      m *= 4;
      m += PW_Start_Address;
      Pattern[i]   = (in_data[l+m] & 0xf0) | poss[in_data[l+m+1]][0];
      Pattern[i+1] = poss[in_data[l+m+1]][1];
      Pattern[i+2] = in_data[l+m+2] | ((in_data[l+m]<<4)&0xf0);
      Pattern[i+3] = in_data[l+m+3];

      /*fprintf ( info, "[%4x][%3ld][%ld]: %2x %2x %2x %2x", j,i,k%4,Pattern[i],Pattern[i+1],Pattern[i+2],Pattern[i+3]);*/

      if ( ( (Pattern[i+2] & 0x0f) == 0x0d ) ||
           ( (Pattern[i+2] & 0x0f) == 0x0b ) )
      {
        /*fprintf ( info, " <-- B or D detected" );*/
        FLAG = ON;
      }
    }
    if ( (FLAG == ON) && ((k%4) == 3) )
    {
      /*fprintf ( info, "\n -> bypassing end of pattern" );*/
      FLAG=OFF;
      while ( (i%1024) != 0)
	    i ++;
      i -= 4;
    }
    k += 1;
    i += 4;
    /*fprintf ( info, "\n" );*/
  }


  /* pattern table */
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<c1 ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
	    Whatever[952+c2] = (uint8_t) i;
  c2 = Whatever[950];
  while ( c2<128 )
  {
    Whatever[952+c2] = 0x00;
    c2+=0x01;
  }
  /*printf ( "ok\n" );*/

  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';
  
  fwrite (Whatever,1084,1,out);
  free ( Whatever );

  /* c1 (+1) is still the number of patterns stored */
  fwrite ( Pattern, (c1-1)*1024, 1, out );

  free (Pattern);

  /*printf ( "Saving sample datas ... " );*/
  Where = PW_Start_Address + SmpAddy;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );
  /*printf ( " ok\n" );*/

  Crap ( "   Promizer 4.0   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
/*  fclose (info);*/

  printf ( "done\n" );
  return; /* useless ... but */
}
