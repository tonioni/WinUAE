/* testPHA() */
/* Rip_PHA() */
/* Depack_PHA() */


#include "globals.h"
#include "extern.h"


short testPHA ( void )
{
  /* test #1 */
  if ( PW_i < 11 )
  {
/*
printf ( "#1 (PW_i:%ld)\n" , PW_i );
*/
    return BAD;
  }

  /* test #2 (volumes,sample addresses and whole sample size) */
  PW_Start_Address = PW_i-11;
  PW_l=0;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* sample size */
    PW_n =(((in_data[PW_Start_Address+PW_j*14]*256)+in_data[PW_Start_Address+PW_j*14+1])*2);
    PW_WholeSampleSize += PW_n;
    /* loop start */
    PW_m =(((in_data[PW_Start_Address+PW_j*14+4]*256)+in_data[PW_Start_Address+PW_j*14+5])*2);

    if ( in_data[PW_Start_Address+3+PW_j*14] > 0x40 )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_m > PW_WholeSampleSize )
    {
/*printf ( "#2,1 (start:%ld) (smp nbr:%ld) (size:%ld) (lstart:%ld)\n"
         , PW_Start_Address,PW_j,PW_n,PW_m );*/
      return BAD;
    }
    PW_k = (in_data[PW_Start_Address+8+PW_j*14]*256*256*256)
         +(in_data[PW_Start_Address+9+PW_j*14]*256*256)
         +(in_data[PW_Start_Address+10+PW_j*14]*256)
         +in_data[PW_Start_Address+11+PW_j*14];
    /* PW_k is the address of this sample data */
    if ( (PW_k < 0x3C0) || (PW_k>PW_in_size) ) 
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  if ( (PW_WholeSampleSize <= 2) || (PW_WholeSampleSize>(31*65535)) )
  {
    /*printf ( "#2,3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test #3 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the WholeSampleSize */
  PW_l = PW_WholeSampleSize + 960;
  PW_k = 0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    PW_m = (in_data[PW_Start_Address+448+PW_j*4]*256*256*256)
      +(in_data[PW_Start_Address+449+PW_j*4]*256*256)
      +(in_data[PW_Start_Address+450+PW_j*4]*256)
      +in_data[PW_Start_Address+451+PW_j*4];
    if ( PW_m > PW_k )
      PW_k = PW_m;
    if ( (PW_m+2) < PW_l )
    {
      /*printf ( "#5 (start:%ld)(add:%ld)(min:%ld)(where:%ld)\n" , PW_Start_Address,PW_m,PW_l, PW_j );*/
      return BAD;
    }
  }
  /* PW_k is the highest pattern data address */


  return GOOD;
}



void Rip_PHA ( void )
{
  /* PW_k is still the highest pattern address ... so, 'all' we */
  /* have to do, here, is to depack the last pattern to get its */
  /* size ... that's all we need. */
  /* NOTE: we dont need to calculate the whole sample size, so */
  PW_m = 0;

  /*  printf ( "(pha)Where : %ld\n"
           "(pha)PW_Start_Address : %ld "
           "(pha)PW_k : %ld\n"
           , PW_i, PW_Start_Address, PW_k );
	   fflush (stdout);*/
  for ( PW_j=0 ; PW_j<256 ; PW_j++ )
  {
    /* 192 = 1100-0000 ($C0) */
    if ( in_data[PW_Start_Address+PW_k+PW_m] < 192 )
    {
      PW_m += 4;
      continue;
    }
    else
    {
      PW_l = 255 - in_data[PW_Start_Address+PW_k+PW_m+1];
      PW_m += 2;
      PW_j += (PW_l-1);
    }
  }
  OutputSize = PW_m + PW_k;

  /*  printf ( "\b\b\b\b\b\b\b\bPHA Packed music found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Pha_packer][0];
  OutName[2] = Extensions[Pha_packer][1];
  OutName[3] = Extensions[Pha_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "PHA Packed music", Pha_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 12);  /* 11 should do but call it "just to be sure" :) */
}



/*
 *   PhaPacker.c   1996-2003 (c) Asle / ReDoX
 *
 * Converts PHA packed MODs back to PTK MODs
 * nth revision :(.
 *
 * update (15 mar 2003)
 * - numerous bugs corrected ... seems to work now ... hum ?
 * update (8 dec 2003)
 * - removed fopen()
*/
void Depack_PHA ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar poss[37][2];
  Uchar *Whole_Pattern_Data;
  Uchar *Pattern;
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
  long Where = PW_Start_Address;
  short Old_cpt[4];
  FILE *out;/*,*info;*/

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  BZERO ( Pats_Address , 128*4 );
  BZERO ( Old_Note_Value , 4*4 );
  BZERO ( Old_cpt , 4*2 );
  BZERO ( MyPatList, 128*sizeof(long));

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*  info = fopen ( "info", "w+b");*/

  for ( i=0 ; i<20 ; i++ )   /* title */
    fwrite ( &c1 , 1 , 1 , out );

  Whatever = (Uchar *) malloc (64);
  for ( i=0 ; i<31 ; i++ )
  {
    BZERO ( Whatever, 64 );

    /*sample name*/
    fwrite ( &Whatever[32] , 22 , 1 , out );

    /* size */
    fwrite ( &in_data[Where] , 2 , 1 , out );
    Whole_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);

    /* finetune */
    c1 = ( Uchar ) (((in_data[Where+12]*256)+in_data[Where+13])/0x48);
    fwrite ( &c1 , 1 , 1 , out );

    /* volume */
    fwrite ( &in_data[Where+3] , 1 , 1 , out );

    /* loop start */
    fwrite ( &in_data[Where+4] , 2 , 1 , out );

    /* loop size */
    fwrite ( &in_data[Where+6] , 2 , 1 , out );
    Where += 14;
  }
  /*printf ( "Whole sample size : %ld\n" , Whole_Sample_Size );*/

  /* bypass those unknown 14 bytes */
  Where += 14;

  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    Where += 4;
    /*fprintf ( info, "%3ld: %ld\n" , i,Pats_Address[i] );*/
    if ( Pats_Address[i] < Start_Pat_Address )Start_Pat_Address = Pats_Address[i];
  }

  Sample_Data_Address = Where;
  /*  printf ( "Sample data address : %ld\n", Sample_Data_Address);*/
  /*printf ( "address of the first pattern : %ld\n" , Start_Pat_Address );*/

  /* pattern datas */
  /* read ALL pattern data */
  Whole_Pattern_Data_Size = OutputSize - Start_Pat_Address;
  Whole_Pattern_Data = (Uchar *) malloc ( Whole_Pattern_Data_Size );
  Where = Start_Pat_Address + PW_Start_Address;
  for (i=0;i<Whole_Pattern_Data_Size;i++)Whole_Pattern_Data[i] = in_data[Where+i];
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
  fwrite ( &in_data[Sample_Data_Address] , Whole_Sample_Size , 1 , out );

  Crap ( "    PhaPacker     " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
