/* testPM40() */
/* Rip_PM40() */
/* Depack_PM40() */

#include "globals.h"
#include "extern.h"


short testPM40 ( void )
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
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}




/*
 *   Promizer_40.c   1997 (c) Asle / ReDoX
 *
 * Converts PM40 packed MODs back to PTK MODs
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
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar PatPos=0x00;
  short Pat_Max=0;
  long tmp_ptr,tmp1,tmp2;
  short Ref_Max=0;
  Uchar Pats_Numbers[128];
  Uchar Pats_Numbers_tmp[128];
  long Pats_Address[128];
  long Pats_Address_tmp[128];
  long Pats_Address_tmp2[128];
  short Pats_PrePointers[64][256];
  Uchar *ReferenceTable;
  Uchar *SampleData;
  Uchar Pattern[128][1024];
  long i=0,j=0,k=0;
  long Total_Sample_Size=0;
  long PatDataSize=0l;
  long SDAV=0l;
  Uchar FLAG=OFF;
  Uchar poss[37][2];
  Uchar Note,Smp;
  /*long Where = PW_Start_Address;*/
  FILE *in,*out;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  in = fopen ( (char *)OutName_final , "r+b" ); /* +b is safe bcoz OutName's just been saved */
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  BZERO ( Pats_Numbers , 128 );
  BZERO ( Pats_Numbers_tmp , 128 );
  BZERO ( Pats_PrePointers , 64*256 );
  BZERO ( Pattern , 128*1024 );
  BZERO ( Pats_Address , 128*4 );
  BZERO ( Pats_Address_tmp , 128*4 );
  for ( i=0 ; i<128 ; i++ )
    Pats_Address_tmp2[i] = 9999l;

  /* write title */
  for ( i=0 ; i<20 ; i++ )   /* title */
    fwrite ( &c1 , 1 , 1 , out );

  /* read and write sample headers */
  /*printf ( "Converting sample headers ... " );*/
  fseek ( in , SAMPLE_DESC , 0 );
  for ( i=0 ; i<31 ; i++ )
  {
    c1 = 0x00;
    for ( j=0 ; j<22 ; j++ ) /*sample name*/
      fwrite ( &c1 , 1 , 1 , out );

    fread ( &c1 , 1 , 1 , in );  /* size */
    fread ( &c2 , 1 , 1 , in );
    Total_Sample_Size += (((c1*256)+c2)*2);
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    fread ( &c1 , 1 , 1 , in );  /* finetune */
    fwrite ( &c1 , 1 , 1 , out );
    fread ( &c1 , 1 , 1 , in );  /* volume */
    fwrite ( &c1 , 1 , 1 , out );
    fread ( &c1 , 1 , 1 , in );  /* loop start */
    fread ( &c2 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    fread ( &c1 , 1 , 1 , in );  /* loop size */
    fread ( &c2 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
  }
  /*printf ( "ok\n" );*/

  /* read and write the size of the pattern list */
  fseek ( in , 7 , 0 ); /* SEEK_SET */
  fread ( &PatPos , 1 , 1 , in );
  fwrite ( &PatPos , 1 , 1 , out );

  /* NoiseTracker restart byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );


  /* pattern addresses */
  fseek ( in , 8 , 0 ); /* SEEK_SET */
  for ( i=0 ; i<128 ; i++ )
  {
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    Pats_Address[i] = (c1*256)+c2;
  }

  /* ordering of patterns addresses */
  /* PatPos contains the size of the pattern list .. */
  /*printf ( "Creating pattern list ... " );*/
  tmp_ptr = 0;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i==0 )
    {
      Pats_Numbers[0] = 0x00;
      tmp_ptr++;
      continue;
    }

    for ( j=0 ; j<i ; j++ )
    {
      if ( Pats_Address[i] == Pats_Address[j] )
      {
        Pats_Numbers[i] = Pats_Numbers[j];
        break;
      }
    }
    if ( j == i )
      Pats_Numbers[i] = tmp_ptr++;
  }

  Pat_Max = tmp_ptr-1;

  /* correct re-order */
  /********************/
  for ( i=0 ; i<c4 ; i++ )
    Pats_Address_tmp[i] = Pats_Address[i];

restart:
  for ( i=0 ; i<c4 ; i++ )
  {
    for ( j=0 ; j<i ; j++ )
    {
      if ( Pats_Address_tmp[i] < Pats_Address_tmp[j] )
      {
        tmp2 = Pats_Numbers[j];
        Pats_Numbers[j] = Pats_Numbers[i];
        Pats_Numbers[i] = tmp2;
        tmp1 = Pats_Address_tmp[j];
        Pats_Address_tmp[j] = Pats_Address_tmp[i];
        Pats_Address_tmp[i] = tmp1;
        goto restart;
      }
    }
  }

  j=0;
  for ( i=0 ; i<c4 ; i++ )
  {
    if ( i==0 )
    {
      Pats_Address_tmp2[j] = Pats_Address_tmp[i];
      continue;
    }

    if ( Pats_Address_tmp[i] == Pats_Address_tmp2[j] )
      continue;
    Pats_Address_tmp2[++j] = Pats_Address_tmp[i];
  }

  for ( c1=0x00 ; c1<c4 ; c1++ )
  {
    for ( c2=0x00 ; c2<c4 ; c2++ )
      if ( Pats_Address[c1] == Pats_Address_tmp2[c2] )
      {
        Pats_Numbers_tmp[c1] = c2;
      }
  }

  for ( i=0 ; i<c4 ; i++ )
    Pats_Numbers[i] = Pats_Numbers_tmp[i];

  /* write pattern table */
  for ( c1=0x00 ; c1<128 ; c1++ )
  {
    fwrite ( &Pats_Numbers[c1] , 1 , 1 , out );
  }
  /*printf ( "ok\n" );*/

  c1 = 'M';
  c2 = '.';
  c3 = 'K';

  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );


  /* a little pre-calc code ... no other way to deal with these unknown
     pattern data sizes ! :( */
  /* so, first, we get the pattern data size .. */
  fseek ( in , ADDRESS_REF_TABLE , 0 ); /* SEEK_SET */
  fread ( &c1 , 1 , 1 , in );
  fread ( &c2 , 1 , 1 , in );
  fread ( &c3 , 1 , 1 , in );
  fread ( &c4 , 1 , 1 , in );
  j = (c1*256*256*256)+(c2*256*256)+(c3*256)+c4;
  PatDataSize = (8 + j) - PATTERN_DATA;
/*  printf ( "Pattern data size : %ld\n" , PatDataSize );*/

  /* go back to pattern data starting address */
  fseek ( in , PATTERN_DATA , 0 ); /* SEEK_SET */
  /* now, reading all pattern data to get the max value of note */
  for ( j=0 ; j<PatDataSize ; j+=2 )
  {
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    if ( ((c1*256)+c2) > Ref_Max )
      Ref_Max = (c1*256)+c2;
  }
/*
  printf ( "* Ref_Max = %d\n" , Ref_Max );
  printf ( "* where : %ld\n" , ftell ( in ) );
*/
  /* read "reference Table" */
  fseek ( in , ADDRESS_REF_TABLE , 0 ); /* SEEK_SET */
  fread ( &c1 , 1 , 1 , in );
  fread ( &c2 , 1 , 1 , in );
  fread ( &c3 , 1 , 1 , in );
  fread ( &c4 , 1 , 1 , in );
  j = (c1*256*256*256)+(c2*256*256)+(c3*256)+c4;
  fseek ( in , 8+j , 0 ); /* SEEK_SET */
/*  printf ( "address of 'reference table' : %ld\n" , ftell (in ) );*/
  Ref_Max += 1;  /* coz 1st value is 0 and will be empty in this table */
  i = Ref_Max * 4; /* coz each block is 4 bytes long */
  ReferenceTable = (Uchar *) malloc ( i );
  BZERO ( ReferenceTable , i );
  fread ( &ReferenceTable[4] , i , 1 , in );

  /* go back to pattern data starting address */
  fseek ( in , PATTERN_DATA , 0 ); /* SEEK_SET */
/*  printf ( "Highest pattern number : %d\n" , Pat_Max );*/

  /*printf ( "Computing the pattern datas " );*/
  k=0;
  for ( j=0 ; j<=Pat_Max ; j++ )
  {
    for ( i=0 ; i<64 ; i++ )
    {
      /* VOICE #1 */

      fread ( &c1 , 1 , 1 , in );
      k += 1;
      fread ( &c2 , 1 , 1 , in );
      k += 1;
      Smp  = ReferenceTable[((c1*256)+c2)*4];
      Note = ReferenceTable[((c1*256)+c2)*4+1];

      Pattern[j][i*16]   = (Smp&0xf0);
      Pattern[j][i*16]   |= poss[Note][0];
      Pattern[j][i*16+1]  = poss[Note][1];
      Pattern[j][i*16+2] = ReferenceTable[((c1*256)+c2)*4+2];
      Pattern[j][i*16+2] |= ((Smp<<4)&0xf0);
      Pattern[j][i*16+3] = ReferenceTable[((c1*256)+c2)*4+3];

      if ( ( (Pattern[j][i*16+2] & 0x0f) == 0x0d ) ||
           ( (Pattern[j][i*16+2] & 0x0f) == 0x0b ) )
      {
        FLAG = ON;
      }

      /* VOICE #2 */

      fread ( &c1 , 1 , 1 , in );
      k += 1;
      fread ( &c2 , 1 , 1 , in );
      k += 1;
      Smp  = ReferenceTable[((c1*256)+c2)*4];
      Note = ReferenceTable[((c1*256)+c2)*4+1];
      
      Pattern[j][i*16+4]   = (Smp&0xf0);
      Pattern[j][i*16+4] |= poss[Note][0];
      Pattern[j][i*16+5]  = poss[Note][1];
      Pattern[j][i*16+6] = ReferenceTable[((c1*256)+c2)*4+2];
      Pattern[j][i*16+6] |= ((Smp<<4)&0xf0);
      Pattern[j][i*16+7] = ReferenceTable[((c1*256)+c2)*4+3];

      if ( ( ( Pattern[j][i*16+6] & 0x0f) == 0x0d ) ||
           ( (Pattern[j][i*16+6] & 0x0f) == 0x0b ) )
      {
        FLAG = ON;
      }

      /* VOICE #3 */

      fread ( &c1 , 1 , 1 , in );
      k += 1;
      fread ( &c2 , 1 , 1 , in );
      k += 1;
      Smp  = ReferenceTable[((c1*256)+c2)*4];
      Note = ReferenceTable[((c1*256)+c2)*4+1];
      
      Pattern[j][i*16+8]   = (Smp&0xf0);
      Pattern[j][i*16+8] |= poss[Note][0];
      Pattern[j][i*16+9]  = poss[Note][1];
      Pattern[j][i*16+10] = ReferenceTable[((c1*256)+c2)*4+2];
      Pattern[j][i*16+10] |= ((Smp<<4)&0xf0);
      Pattern[j][i*16+11]= ReferenceTable[((c1*256)+c2)*4+3];

      if ( ( (Pattern[j][i*16+10] & 0x0f) == 0x0d ) ||
           ( (Pattern[j][i*16+10] & 0x0f) == 0x0b ) )
      {
        FLAG = ON;
      }

      /* VOICE #4 */

      fread ( &c1 , 1 , 1 , in );
      k += 1;
      fread ( &c2 , 1 , 1 , in );
      k += 1;
      Smp  = ReferenceTable[((c1*256)+c2)*4];
      Note = ReferenceTable[((c1*256)+c2)*4+1];
      
      Pattern[j][i*16+12]   = (Smp&0xf0);
      Pattern[j][i*16+12] |= poss[Note][0];
      Pattern[j][i*16+13]  = poss[Note][1];
      Pattern[j][i*16+14] = ReferenceTable[((c1*256)+c2)*4+2];
      Pattern[j][i*16+14] |= ((Smp<<4)&0xf0);
      Pattern[j][i*16+15]= ReferenceTable[((c1*256)+c2)*4+3];

      if ( ( (Pattern[j][i*16+14] & 0x0f) == 0x0d ) ||
           ( (Pattern[j][i*16+14] & 0x0f) == 0x0b ) )
      {
        FLAG = ON;
      }

      if ( FLAG == ON )
      {
        FLAG=OFF;
        break;
      }
    }
    fwrite ( Pattern[j] , 1024 , 1 , out );
    /*printf ( "." );*/
  }
  free ( ReferenceTable );
  /*printf ( " ok\n" );*/


  /* get address of sample data .. and go there */
  /*printf ( "Saving sample datas ... " );*/
  fseek ( in , ADDRESS_SAMPLE_DATA , 0 ); /* SEEK_SET */
  fread ( &c1 , 1 , 1 , in );
  fread ( &c2 , 1 , 1 , in );
  fread ( &c3 , 1 , 1 , in );
  fread ( &c4 , 1 , 1 , in );
  SDAV = (c1*256*256*256)+(c2*256*256)+(c3*256)+c4;
  fseek ( in , 4 + SDAV , 0 ); /* SEEK_SET */


  /* read and save sample data */
/*  printf ( "out: where before saving sample data : %ld\n" , ftell ( out ) );*/
/*  printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/
  SampleData = (Uchar *) malloc ( Total_Sample_Size );
  fread ( SampleData , Total_Sample_Size , 1 , in );
  fwrite ( SampleData , Total_Sample_Size , 1 , out );
  free ( SampleData );
  /*printf ( " ok\n" );*/

  Crap ( "   Promizer 4.0   " , BAD , BAD , out );

  fflush ( in );
  fflush ( out );
  fclose ( in );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
