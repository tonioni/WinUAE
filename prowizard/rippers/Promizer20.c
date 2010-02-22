

/* testPM2() */
/* Rip_PM20() */
/* Depack_PM20() */

#include "globals.h"
#include "extern.h"


short testPM2 ( void )
{
  PW_Start_Address = PW_i;
  /* test 1 */
  if ( (PW_Start_Address + 5714) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  /*if ( in_data[PW_Start_Address+5094] != 0x03 )*/
    /* not sure in fact ... */
    /* well, it IS the frequency table, it always seem */
    /* to be the 'standard one .. so here, there is 0358h */
  /*  {
    return BAD;
    }*/
  
  /* test 3 */
  if ( in_data[PW_Start_Address+5461] > 0x40 )
    /* testing a volume */
  {
    return BAD;
  }

  return GOOD;
}



void Rip_PM20 ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+5458+PW_k*8]*256)+in_data[PW_Start_Address+5459+PW_k*8])*2);
  PW_j = (in_data[PW_Start_Address+5706]*256*256*256)+(in_data[PW_Start_Address+5707]*256*256)+(in_data[PW_Start_Address+5708]*256)+in_data[PW_Start_Address+5709];
  OutputSize = PW_WholeSampleSize + 5198 + PW_j;

  CONVERT = GOOD;
  Save_Rip ( "Promizer 2.0 module", Promizer_20 );
  
  if ( Save_Status == GOOD )
    PW_i += 2;  /* 1 should do but call it "just to be sure" :) */
}



/*
 *   Promizer_20.c   1997 (c) Asle / ReDoX
 *
 * Converts PM20 packed MODs back to PTK MODs
 *
 * update 20 mar 2003 (it's war time again .. brrrr)
 * - removed all open() funcs.
 * - optimized more than quite a bit (src 4kb shorter !)
 *
 * update 20091113
 * - fixed patternlist generation and cleaned a bit
 *
 * update 20100212-20100216
 * - fixed endless loop bug (used PW_i instead of i ...)
 * - correct conversion when there's unused patterns stored .. they are kept ;)
*/

#define ON  0
#define OFF 1
#define AFTER_REPLAY_CODE   5198
#define SAMPLE_DESC         5458
#define ADDRESS_SAMPLE_DATA 5706
#define ADDRESS_REF_TABLE   5710
#define PATTERN_DATA        5714

void Depack_PM20 ( void )
{
  //Uchar c1=0x00;
  short Ref_Max=0;
  long Pats_Address[128],Pats_Address_infile[128];
  Uchar NOP=0x00; /* number of pattern */
  Uchar *ReferenceTable;
  Uchar *Pattern;
  long i=0,j;
  long Total_Sample_Size=0;
  long PatDataSize=0l;
  //long SDAV=0l;
  Uchar FLAG=OFF;
  Uchar poss[37][2];
  Uchar Note,Smp;
  Uchar *Whatever;
  Uchar *WholePatternData;
  long Where = PW_Start_Address;
  FILE *out;/*,*info;*/

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b");*/

  BZERO ( Pats_Address , 128*4 );
  BZERO ( Pats_Address_infile , 128*4 );

  Whatever = (Uchar *) malloc (1085);
  BZERO (Whatever, 1085);

  /* bypass replaycode routine */
  Where += SAMPLE_DESC;

  for ( i=0 ; i<31 ; i++ )
  {
    Whatever[i*30+42] = in_data[Where];
    Whatever[i*30+43] = in_data[Where+1];
    Total_Sample_Size += (((Whatever[i*30+42]*256)+Whatever[i*30+43])*2);
    Whatever[i*30+44] = in_data[Where+2]/2;
    Whatever[i*30+45] = in_data[Where+3];
    Whatever[i*30+46] = in_data[Where+4];
    Whatever[i*30+47] = in_data[Where+5];
    Whatever[i*30+48] = in_data[Where+6];
    Whatever[i*30+49] = in_data[Where+7];
    if ( (Whatever[i*30+48] == 0x00) && (Whatever[i*30+49] == 0x00) )Whatever[i*30+49] = 0x01;

    Where += 8;
  }

  /* read "used" size of pattern table */
  Where = PW_Start_Address + AFTER_REPLAY_CODE + 2;
  NOP = ((in_data[Where]*256)+in_data[Where+1])/2;
  Where += 2;
  /*printf ( "Number of pattern in pattern list : %d\n" , NOP );*/

  /* write size of pattern list */
  Whatever[950] = NOP;

  /* NoiseTracker restart byte */
  Whatever[951] = 0x7f;

  /* read pattern addys */
  for ( i=0 ; i<NOP ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
    //printf ( "[%3ld] : %ld\n", i, Pats_Address[i] );
  }

  /* write pattern table */
  /* doesn't work if there are unused patterns */
/*  PW_k = PW_l = 0;*/
/*  for (PW_j=0; PW_j<NOP ; PW_j++)*/
/*  {*/
/*    PW_m = 0x7fffffff; *//* min */
    /*search for min */
/*    for (i=0; i<NOP ; i++)
      if ((Pats_Address[i]<PW_m) && (Pats_Address[i]>PW_k))
	    PW_m = Pats_Address[i];*/
    /* if PW_k == PW_m then an already ref was found */
/*    if (PW_k == PW_m)
      continue;*/
    /* PW_m is the next minimum */
/*    PW_k = PW_m;
    for (i=0; i<NOP ; i++)
      if (Pats_Address[i] == PW_k)
	Whatever[952+i] = (unsigned char)PW_l;
    PW_l++;
  }*/
  /* PW_l is now the number of pattern saved (+1) */
  /* but I'll retrieve the official one ... */

  /* write tag */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';

/*  fwrite ( Whatever, 1, 1084, out );*/

  /* a little pre-calc code ... no other way to deal with these unknown pattern data sizes ! :( */
  /* so, first, we get the pattern data size .. */
  Where = PW_Start_Address + ADDRESS_REF_TABLE;
  PW_j = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  PatDataSize = (AFTER_REPLAY_CODE + PW_j) - PATTERN_DATA;
  /*fprintf ( info, "Pattern data size : %ld\n" , PatDataSize );*/

  /* go back to pattern data starting address */
  Where = PW_Start_Address + PATTERN_DATA;

  /* now, reading all pattern data to get the max value of note */
  WholePatternData = (Uchar *) malloc (PatDataSize+1);
  BZERO (WholePatternData, PatDataSize+1);
  for ( PW_j=0 ; PW_j<PatDataSize ; PW_j+=2 )
  {
    WholePatternData[PW_j] = in_data[Where+PW_j];
    WholePatternData[PW_j+1] = in_data[Where+PW_j+1];
    if ( ((WholePatternData[PW_j]*256)+WholePatternData[PW_j+1]) > Ref_Max )
      Ref_Max = ((WholePatternData[PW_j]*256)+WholePatternData[PW_j+1]);
  }

  /* read "reference Table" */
  Where = PW_Start_Address + ADDRESS_REF_TABLE;
  PW_j = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + AFTER_REPLAY_CODE + PW_j;

  Ref_Max += 1;  /* coz 1st value is 0 ! */
  i = Ref_Max * 4; /* coz each block is 4 bytes long */
  ReferenceTable = (Uchar *) malloc ( i );
  BZERO ( ReferenceTable, i );
  for ( PW_j=0 ; PW_j<i ; PW_j++) ReferenceTable[PW_j] = in_data[Where+PW_j];

  /* go back to pattern data starting address */
  Where = PW_Start_Address + PATTERN_DATA;


  PW_k=0; /* current note number */
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  PW_l = 1; /* nbr of patterns stored */
  Pats_Address_infile[0] = 0;
  for ( PW_j=0 ; PW_j<PatDataSize ; PW_j+=2 )
  {
    if ( ((i%1024) == 0) && (PW_j>0) )
    {
      /*printf ("-%lx-",PW_j);*/
      Pats_Address_infile[PW_l] = PW_j;
      PW_l++;
    }
    PW_m = ((WholePatternData[PW_j]*256)+WholePatternData[PW_j+1])*4;

    Smp  = ReferenceTable[PW_m];
    Smp  = Smp >> 2;
    Note = ReferenceTable[PW_m+1];
      
    Pattern[i]   = (Smp&0xf0);
    Pattern[i]   |= poss[(Note/2)][0];
    Pattern[i+1] = poss[(Note/2)][1];
    Pattern[i+2] = ReferenceTable[PW_m+2];
    Pattern[i+2] |= ((Smp<<4)&0xf0);
    Pattern[i+3] = ReferenceTable[PW_m+3];
    /*fprintf ( info, "[%4ld][%ld][%ld] %2x %2x %2x %2x",i,k%4,j,Pattern[i],Pattern[i+1],Pattern[i+2],Pattern[i+3] );*/

    if ( ( (Pattern[i+2] & 0x0f) == 0x0d ) ||
	 ( (Pattern[i+2] & 0x0f) == 0x0b ) )
    {
      /*fprintf ( info, " <- D or B detected" );*/
      FLAG = ON;
    }
    if ( (FLAG == ON) && ((PW_k%4) == 3) )
    {
      /*fprintf ( info, "\n -> bypassing end of pattern" );*/
      FLAG=OFF;
      while ( (i%1024) != 0)
	    i ++;
      i -= 4;
    }

    PW_k += 1;
    i += 4;
    /*fprintf ( info, "\n" );*/
  }

  /* let's update now the pattern list */
  for (i=0; i<NOP; i++)
  {
    /*printf ("\n[%2ld][%lx]:",i,Pats_Address[i]);*/
    for (j=0; j<PW_l; j++)
    {
      /*printf ("%lx-",Pats_Address_infile[j]);*/
      if (Pats_Address_infile[j] == Pats_Address[i])
      {
        /*printf ("done");*/
        Whatever[952+i] = j;
        break;
      }
    }
  }
  fwrite ( Whatever, 1, 1084, out );


  free ( ReferenceTable );
  free ( WholePatternData );

  free ( Whatever );

  /* write pattern data */
  /* PW_l is still the number of patterns stored */
  fwrite ( Pattern, PW_l*1024, 1, out );

  /* get address of sample data .. and go there */
  Where = PW_Start_Address + ADDRESS_SAMPLE_DATA;
  i = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
  Where = PW_Start_Address + AFTER_REPLAY_CODE + i;


  /* read and save sample data */
  /*fprintf ( info, "Total sample size : %ld\n" , Total_Sample_Size );*/
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "   Promizer 2.0   " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*fclose (info );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
