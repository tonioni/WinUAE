/* testPM10c() */
/* Rip_PM10c() */
/* Depack_PM10c() */

#include "globals.h"
#include "extern.h"



int16_t	 testPM10c ( void )
{
  /* test 1 */
  if ( (PW_Start_Address + 4452) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  if ( in_data[PW_Start_Address + 21] != 0xce )
  {
    return BAD;
  }

  /* test 3 */
  PW_j = (in_data[PW_Start_Address+4452]*256*256*256)+(in_data[PW_Start_Address+4453]*256*256)+(in_data[PW_Start_Address+4454]*256)+in_data[PW_Start_Address+4455];
  if ( (PW_Start_Address + PW_j + 4452) > PW_in_size )
  {
    return BAD;
  }

  /* test 4 */
  PW_k = (in_data[PW_Start_Address+4712]*256)+in_data[PW_Start_Address+4713];
  PW_l = PW_k/4;
  PW_l *= 4;
  if ( PW_l != PW_k )
  {
    return BAD;
  }

  /* test 5 */
  if ( in_data[PW_Start_Address + 36] != 0x10 )
  {
    return BAD;
  }

  /* test 6 */
  if ( in_data[PW_Start_Address + 37] != 0xFC )
  {
    return BAD;
  }

  return GOOD;
}



void Rip_PM10c ( void )
{
  /* we NEED this 'PW_j' value found while testing !,so we keep it :) */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+4460+8*PW_k]*256)+in_data[PW_Start_Address+4461+8*PW_k])*2);
  OutputSize = 4456 + PW_j + PW_WholeSampleSize;

  CONVERT = GOOD;
  Save_Rip ( "Promizer 1.0c module", Promizer_10c );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}



/*
 *   Promizer_10c.c   1997 (c) Asle / ReDoX
 *
 * Converts PM10c packed MODs back to PTK MODs
 *
 * update 20 mar 2003 (it's war time again .. brrrr)
 * - removed all open() funcs.
 * - optimized more than quite a bit (src is 5kb int16_t	er !)
 *
 * update 08 jan 2010
 * - bug fix in patternlist generation
*/

#define ON  0
#define OFF 1

#define PATTERN_DATA_ADDY 5222

void Depack_PM10c ( void )
{
  uint8_t c1=0x00,c2=0x00;
  int16_t	 Ref_Max=0;
  int32_t	 Pats_Address[128];
  int32_t	 Read_Pats_Address[128];
  uint8_t NOP=0x00; /* number of pattern */
  uint8_t *ReferenceTable;
  uint8_t *Pattern;
  int32_t	 i=0,j=0,k=0,l=0,m=0;
  int32_t	 Total_Sample_Size=0;
  int32_t	 PatDataSize=0l;
  int32_t	 SDAV=0l;
  uint8_t FLAG=OFF;
  uint8_t Smp_Fine_Table[31];
  uint8_t poss[37][2];
  uint8_t OldSmpValue[4];
  uint8_t *Whatever;
  uint8_t *WholePatternData;
  int32_t	 Where = PW_Start_Address;
  int16_t	 Period;
  FILE *out;/*,*info;*/

  #include "tuning.h"
  fillPTKtable(poss);


  if ( Save_Status == BAD )
    return;

  BZERO ( Smp_Fine_Table , 31 );
  BZERO ( OldSmpValue , 4 );
  BZERO ( Pats_Address , 128*4 );

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b");*/

  Whatever = (uint8_t *) malloc (1085);
  BZERO (Whatever, 1085);
  /* title */
  /*fwrite ( &Whatever[32] , 20 , 1 , out );*/

  /* bypass replaycode routine */
  Where = PW_Start_Address + 4460;

  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    /*fwrite ( &Whatever[32] ,22 , 1 , out );*/
    Whatever[42+30*i] = in_data[Where];
    Whatever[43+30*i] = in_data[Where+1];
    Whatever[44+30*i] = in_data[Where+2];
    Whatever[45+30*i] = in_data[Where+3];
    Whatever[46+30*i] = in_data[Where+4];
    Whatever[47+30*i] = in_data[Where+5];
    Whatever[48+30*i] = in_data[Where+6];
    Whatever[49+30*i] = in_data[Where+7];
    /*fwrite ( &in_data[Where], 8, 1, out );*/
    /* whole sample size */
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    /* finetune */
    Smp_Fine_Table[i] = in_data[Where+2];
    Where += 8;
  }

  /* read patterns addys */
  Where = PW_Start_Address + 4710;
  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    /*fprintf ( info, "Pat[%3ld]: %ld\n",i,Pats_Address[i]);*/
    Where += 4;
  }

  /* --------------------- */
  /* a little pre-calc code ... no other way to deal with these unknown pattern data sizes ! :( */
  Where = PW_Start_Address + 4456;
  PatDataSize = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  /*fprintf ( info, "PatDataSize : %ld\n" , PatDataSize );*/

  /* go back to pattern data starting address */
  Where = PW_Start_Address + PATTERN_DATA_ADDY;

  /* now, reading all pattern data to get the max value of note */
  WholePatternData = (uint8_t *) malloc (PatDataSize+1);
  BZERO (WholePatternData, PatDataSize+1);
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    WholePatternData[j] = in_data[Where+j];
    WholePatternData[j+1] = in_data[Where+j+1];
    if ( ((WholePatternData[j]*256)+WholePatternData[j+1]) > Ref_Max )
      Ref_Max = ((WholePatternData[j]*256)+WholePatternData[j+1]);
  }
  Where += PatDataSize;

  /*fprintf ( info, "highest ref number (Ref_Max) = %d\n" , Ref_Max );*/

  /* read "reference Table" */
  Ref_Max += 1;  /* coz 1st value is 0 ! */
  i = Ref_Max * 4; /* coz each block is 4 bytes int32_t	 */
  ReferenceTable = (uint8_t *) malloc ( i+1 );
  BZERO ( ReferenceTable, i+1 );
  for ( j=0 ; j<i ; j++) ReferenceTable[j] = in_data[Where+j];

  /*fprintf ( info, "REFERENCE TABLE SIZE ((Ref_Max+1)*4) : %ld\n" , i );*/

  c1=0; /* will count patterns */
  k=0; /* current note number */
  Pattern = (uint8_t *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    if ( (i%1024) == 0 )
    {
      Read_Pats_Address[c1] = j;
      c1 += 0x01;
      /*fprintf ( info, " -> new pattern %2d (addy :%ld)\n", c1, j+5222 );*/
    }

    m = ((WholePatternData[j]*256)+WholePatternData[j+1])*4;
    Pattern[i]   = ReferenceTable[m];
    Pattern[i+1] = ReferenceTable[m+1];
    Pattern[i+2] = ReferenceTable[m+2];
    Pattern[i+3] = ReferenceTable[m+3];

    /*fprintf ( info, "[%4x][%3ld][%ld]: %2x %2x %2x %2x", j,i,k%4,Pattern[i],Pattern[i+1],Pattern[i+2],Pattern[i+3]);*/

    c2 = ((Pattern[i+2]>>4)&0x0f) | (Pattern[i]&0xf0);
    if ( c2 != 0x00 )
    {
      OldSmpValue[k%4] = c2;
    }
    Period =  ((Pattern[i]&0x0f)*256)+Pattern[i+1];
    if ( (Period != 0) && (Smp_Fine_Table[OldSmpValue[k%4]-1] != 0x00) )
    {
      for ( l=0 ; l<36 ; l++ )
	if ( Tuning[Smp_Fine_Table[OldSmpValue[k%4]-1]][l] == Period )
        {
	  Pattern[i]   &= 0xf0;
	  Pattern[i]   |= poss[l+1][0];
	  Pattern[i+1]  = poss[l+1][1];
	  break;
	}
    }
    
    if ( ( (Pattern[i+2] & 0x0f) == 0x0d ) ||
	 ( (Pattern[i+2] & 0x0f) == 0x0b ) )
    {
      /*fprintf ( info, " <-- B or D detected" );*/
      FLAG = ON;
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
  free ( ReferenceTable );
  free ( WholePatternData );

  /* pattern table lenght */
  Where = PW_Start_Address + 4708;
  NOP = ((in_data[Where]*256)+in_data[Where+1])/4;
  Whatever[950] = NOP;
  /*fwrite ( &NOP , 1 , 1 , out );*/

  Whatever[951] = 0x7f;
  /*Whatever[0] = 0x7f;*/
  /*fwrite ( &Whatever[0], 1, 1, out );*/

  /* write pattern table */
  /*BZERO ( Whatever, 128 );*/
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<c1 ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
	    Whatever[952+c2] = (uint8_t) i;
	    /*Whatever[c2] = (uint8_t) i;*/
  while ( NOP<128 )
  {
    Whatever[952+NOP] = 0x00;
    NOP++;
  }
  /*fwrite ( &Whatever[0], 128, 1, out );*/

  /* write tag */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';

  /*fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  fwrite ( &Whatever[2] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );*/
  fwrite (Whatever, 1, 1084, out);

  free ( Whatever );

  /* write pattern data */
  /* c1 is still the number of patterns stored */
  fwrite ( Pattern, c1*1024, 1, out );

  free (Pattern);

  Where = PW_Start_Address + 4452;
  SDAV = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + 4456 + SDAV;


  /* sample data */
  /*printf ( "Total sample size : %ld\n" , Total_Sample_Size );*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );


  Crap ( "  Promizer v1.0c  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*fclose ( info );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
