/*
 *   PhaPacker.c   1996-2003 (c) Asle / ReDoX
 *
 * Converts PHA packed MODs back to PTK MODs
 * nth revision :(.
 *
 * update (15 mar 2003)
 * - numerous bugs corrected ... seems to work now ... hum ?
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Depack_PHA ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar poss[37][2];
  Uchar *Whole_Pattern_Data;
  Uchar *Pattern;
  Uchar *Sample_Data;
  Uchar *Whatever;
  Uchar Old_Note_Value[4][4];
  Uchar Note,Smp,Fx,FxVal;
  Uchar PatMax=0x00;
  long MyPatList[128];
  long Pats_Address[128];
  long i=0,j=0,k=0;
  long Start_Pat_Address=9999999l;
  long Whole_Pattern_Data_Size;
  long Whole_Sample_Size=0;
  long Sample_Data_Address;
  short Old_cpt[4];
  FILE *in,*out;/*,*info;*/

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif
#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  BZERO ( Pats_Address , 128*4 );
  BZERO ( Old_Note_Value , 4*4 );
  BZERO ( Old_cpt , 4*2 );
  BZERO ( MyPatList, 128*sizeof(long));

  in = fopen ( (char *)OutName_final , "r+b" ); /* +b is safe bcoz OutName's just been saved */
  if (!in)
      return;
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;
  /*  info = fopen ( "info", "w+b");*/

  for ( i=0 ; i<20 ; i++ )   /* title */
    fwrite ( &c1 , 1 , 1 , out );

  Whatever = (Uchar *) malloc (64);
  for ( i=0 ; i<31 ; i++ )
  {
    BZERO ( Whatever, 64 );
    fread ( &Whatever[0], 14, 1, in );

    /*sample name*/
    fwrite ( &Whatever[32] , 22 , 1 , out );

    /* size */
    fwrite ( &Whatever[0] , 2 , 1 , out );
    Whole_Sample_Size += (((Whatever[0]*256)+Whatever[1])*2);

    /* finetune */
    c1 = ( Uchar ) (((Whatever[12]*256)+Whatever[13])/0x48);
    fwrite ( &c1 , 1 , 1 , out );

    /* volume */
    fwrite ( &Whatever[3] , 1 , 1 , out );

    /* loop start */
    fwrite ( &Whatever[4] , 2 , 1 , out );

    /* loop size */
    fwrite ( &Whatever[6] , 2 , 1 , out );
  }
  /*printf ( "Whole sample size : %ld\n" , Whole_Sample_Size );*/

  /* bypass those unknown 14 bytes */
  fseek ( in , 14 , 1 ); /* SEEK_CUR */

  for ( i=0 ; i<128 ; i++ )
  {
    fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    fread ( &c3 , 1 , 1 , in );
    fread ( &c4 , 1 , 1 , in );
    Pats_Address[i] = (c1*256*256*256)+(c2*256*256)+(c3*256)+c4;
    /*fprintf ( info, "%3ld: %ld\n" , i,Pats_Address[i] );*/
    if ( Pats_Address[i] < Start_Pat_Address )Start_Pat_Address = Pats_Address[i];
  }

  Sample_Data_Address = ftell ( in );
  /*  printf ( "Sample data address : %ld\n", Sample_Data_Address);*/
  /*printf ( "address of the first pattern : %ld\n" , Start_Pat_Address );*/

  /* pattern datas */
  /* read ALL pattern data */
  fseek ( in , 0 , 2 ); /* SEEK_END */
  Whole_Pattern_Data_Size = ftell (in) - Start_Pat_Address;
  Whole_Pattern_Data = (Uchar *) malloc ( Whole_Pattern_Data_Size );
  fseek ( in , Start_Pat_Address , 0 ); /* SEEK_SET */
  fread ( Whole_Pattern_Data , Whole_Pattern_Data_Size , 1 , in );
  /*  printf ( "Whole pattern data size : %ld\n" , Whole_Pattern_Data_Size );*/
  Pattern = (Uchar *) malloc ( 65536 );
  BZERO ( Pattern , 65536 );


  j=0;k=0;c1=0x00;
  for ( i=0 ; i<Whole_Pattern_Data_Size ; i++ )
  {
    if ((k%256)==0)
    {
      MyPatList[c1] = Start_Pat_Address+i;
      /*      fprintf (info, "-> new patter [addy:%ld] [nbr:%d]\n", MyPatList[c1], c1);*/
      c1 += 0x01;
    }
    if ( Whole_Pattern_Data[i] == 0xff )
    {
      i += 1;
      /*      Old_cpt[(k+3)%4] = 0xff - Whole_Pattern_Data[i];*/
      Old_cpt[(k-1)%4] = 0xff - Whole_Pattern_Data[i];
      /*      fprintf (info, "-> count set to [%d] for voice [%ld]\n",Old_cpt[(k-1)%4],(k-1)%4 );*/
      /*k += 1;*/
      continue;
    }
    if ( Old_cpt[k%4] != 0 )
    {
      Smp    = Old_Note_Value[k%4][0];
      Note   = Old_Note_Value[k%4][1];
      Fx     = Old_Note_Value[k%4][2];
      FxVal  = Old_Note_Value[k%4][3];
      /*      fprintf ( info, "[%5ld]-[%ld] %2x %2x %2x %2x [%ld] [%6ld] (count : %d)\n",i,k%4,Smp,Note,Fx,FxVal,j,MyPatList[c1-1],Old_cpt[k%4] );*/
      Old_cpt[k%4] -= 1;

      Pattern[j]    = Smp&0xf0;
      Pattern[j]   |= poss[(Note/2)][0];
      Pattern[j+1]  = poss[(Note/2)][1];
      Pattern[j+2]  = (Smp<<4)&0xf0;
      Pattern[j+2] |= Fx;
      Pattern[j+3]  = FxVal;
      k+=1;
      j+=4;
      i-=1;
      continue;
    }
    Smp   = Whole_Pattern_Data[i];
    Note  = Whole_Pattern_Data[i+1];
    Fx    = Whole_Pattern_Data[i+2];
    FxVal = Whole_Pattern_Data[i+3];
    Old_Note_Value[k%4][0] = Smp;
    Old_Note_Value[k%4][1] = Note;
    Old_Note_Value[k%4][2] = Fx;
    Old_Note_Value[k%4][3] = FxVal;
    /*    fprintf ( info, "[%5ld]-[%ld] %2x %2x %2x %2x [%ld] [%6ld]\n",i,k%4,Smp,Note,Fx,FxVal,j, MyPatList[c1-1]);*/
    /*    fflush (info);*/
    i += 3;
    Pattern[j]    = Smp&0xf0;
    Pattern[j]   |= poss[(Note/2)][0];
    Pattern[j+1]  = poss[(Note/2)][1];
    Pattern[j+2]  = (Smp<<4)&0xf0;
    Pattern[j+2] |= Fx;
    Pattern[j+3]  = FxVal;
    k+=1;
    j+=4;
  }
  PatMax = c1;

  /*
fprintf ( info , "pats address      pats address tmp\n" );
for ( i=0 ; i<128 ; i++ )
{
  fprintf ( info , "%3ld: %6ld   %ld [%ld]\n" , i , Pats_Address[i] , Pats_Address_tmp[i],MyPatList[i] );
}
fflush ( info );*/

  /* try to get the number of pattern in pattern list */
  for ( c1=128 ; c1>0x00 ; c1-=0x01 )
    if ( Pats_Address[c1] != Pats_Address[127] )
      break;

  /* write this value */
  c1 += 1;
  fwrite ( &c1 , 1 , 1 , out );

  /* ntk restart byte */
  c2 = 0x7f;
  fwrite ( &c2 , 1 , 1 , out );

  /* write pattern list */
  for ( i=0 ; i<128 ; i++ )
  {
    for (c1=0x00; Pats_Address[i]!=MyPatList[c1];c1+=0x01);
    fwrite ( &c1 , 1 , 1 , out );
  }


  /* ID string */
  c1 = 'M';
  c2 = '.';
  c3 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );


  fwrite ( Pattern , PatMax*1024 , 1 , out );
  free ( Whole_Pattern_Data );
  free ( Pattern );

  /* Sample data */
  fseek ( in , Sample_Data_Address , 0 ); /* SEEK_SET */
  Sample_Data = (Uchar *) malloc ( Whole_Sample_Size );
  fread ( Sample_Data , Whole_Sample_Size , 1 , in );
  fwrite ( Sample_Data , Whole_Sample_Size , 1 , out );
  free ( Sample_Data );

  Crap ( "    PhaPacker     " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , "  -[PhaPacker to]-  " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[Protracker]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  fflush ( in );
  fflush ( out );
  fclose ( in );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
