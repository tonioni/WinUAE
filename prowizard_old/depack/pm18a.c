/*
 *   Promizer_18a.c   1997 (c) Asle / ReDoX
 *
 * Converts PM18a packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * update 20 mar 2003 (it's war time again .. brrrr)
 * - removed all open() funcs.
 * - optimized more than quite a bit (src is 5kb shorter !)
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

#define ON  0
#define OFF 1

void Depack_PM18a ( void )
{
  Uchar c1=0x00,c2=0x00;
  short Ref_Max=0;
  long Pats_Address[128];
  long Read_Pats_Address[128];
  Uchar NOP=0x00; /* number of pattern */
  Uchar *ReferenceTable;
  Uchar *Pattern;
  long i=0,j=0,k=0,l=0,m=0;
  long Total_Sample_Size=0;
  long PatDataSize=0l;
  long SDAV=0l;
  Uchar FLAG=OFF;
  Uchar Smp_Fine_Table[31];
  Uchar poss[37][2];
  Uchar OldSmpValue[4];
  short Period;
  Uchar *Whatever;
  long Where = PW_Start_Address;
  Uchar *WholePatternData;
  FILE *out;

#ifdef DOS
  #include "..\include\tuning.h"
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/tuning.h"
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  /*in = fopen ( OutName_final , "r+b" );*/ /* +b is safe bcoz OutName's just been saved */
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  BZERO ( Smp_Fine_Table , 31 );
  BZERO ( OldSmpValue , 4 );
  BZERO ( Pats_Address , 128*4 );

  Whatever = (Uchar *) malloc (128);
  BZERO (Whatever, 128);
  /* title */
  fwrite ( &Whatever[0] , 20 , 1 , out );

  /* bypass replaycode routine */
  /*fseek ( in , 4464 , 0 );*/  /* SEEK_SET */
  Where = PW_Start_Address + 4464;

  for ( i=0 ; i<31 ; i++ )
  {
    
    /*sample name*/
    fwrite ( &Whatever[32] , 22 , 1 , out );
    fwrite ( &in_data[Where], 8, 1, out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    Smp_Fine_Table[i] = in_data[Where+2];
    Where += 8;
  }

  /* pattern table lenght */
  NOP = ((in_data[Where]*256)+in_data[Where+1])/4;
  fwrite ( &NOP , 1 , 1 , out );
  Where += 2;

  /*printf ( "Number of patterns : %d\n" , NOP );*/

  /* NoiseTracker restart byte */
  Whatever[0] = 0x7f;
  fwrite ( &Whatever[0] , 1 , 1 , out );

  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    Where += 4;
  }


  /* a little pre-calc code ... no other way to deal with these unknown pattern data sizes ! :( */
  Where = PW_Start_Address + 4460;
  PatDataSize = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  /* go back to pattern data starting address */
  Where = PW_Start_Address + 5226;
  /* now, reading all pattern data to get the max value of note */
  WholePatternData = (Uchar *) malloc (PatDataSize+1);
  BZERO (WholePatternData, PatDataSize+1);
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    WholePatternData[j] = in_data[Where+j];
    WholePatternData[j+1] = in_data[Where+j+1];
    if ( ((WholePatternData[j]*256)+WholePatternData[j+1]) > Ref_Max )
      Ref_Max = ((WholePatternData[j]*256)+WholePatternData[j+1]);
  }
  Where += PatDataSize;

  /* read "reference Table" */
  Ref_Max += 1;  /* coz 1st value is 0 ! */
  i = Ref_Max * 4; /* coz each block is 4 bytes long */
  ReferenceTable = (Uchar *) malloc ( i );
  BZERO ( ReferenceTable, i+1 );
  for ( j=0 ; j<i ; j++) ReferenceTable[j] = in_data[Where+j];


  c1=0; /* will count patterns */
  k=0; /* current note number */
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    if ( (i%1024) == 0 )
    {
      Read_Pats_Address[c1] = j;
      c1 += 0x01;
      /*fprintf ( info, " -> new pattern %2d (addy :%ld)\n", c1, j+5226 );*/
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

  /* write pattern table */
  BZERO ( Whatever, 128 );
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<NOP ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
	Whatever[c2] = (Uchar) i;
  while ( i<128 )
    Whatever[i++] = 0x00;
  fwrite ( &Whatever[0], 128, 1, out );

  /* write tag */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';

  fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  fwrite ( &Whatever[2] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );

  free ( Whatever );

  /* write pattern data */
  /* c1 is still the number of patterns stored */
  fwrite ( Pattern, c1*1024, 1, out );

  free (Pattern);

  Where = PW_Start_Address + 4456;
  SDAV = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + 4460 + SDAV;


  /* smp data */

  /*printf ( "Total sample size : %ld\n" , Total_Sample_Size );*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "  Promizer v1.8a  " , BAD , BAD , out );
  /*
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "  -[PM18a to Ptk]-  " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
