/* testTitanicsPlayer() */
/* Rip_TitanicsPlayer() */
/* Depack_TitanicsPlayer() */

#include "globals.h"
#include "extern.h"


short testTitanicsPlayer ( void )
{
  if ( PW_i < 7 )
    return BAD;
  PW_Start_Address = PW_i-7;

  /* test samples */
  PW_n = PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+7+PW_k*12] > 0x40 )
    {
      /*printf ("#1 : (start : %ld)\n",PW_Start_Address);*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+6+PW_k*12] != 0x00 )
    {
      /*printf ("#1.1 : (start : %ld)\n",PW_Start_Address);*/
      return BAD;
    }

    PW_o = ((in_data[PW_Start_Address+PW_k*12]*256*256*256) +
            (in_data[PW_Start_Address+1+PW_k*12]*256*256)+
            (in_data[PW_Start_Address+2+PW_k*12]*256)+
            in_data[PW_Start_Address+3+PW_k*12]);
    if ((PW_o > PW_in_size) || ((PW_o < 180) && (PW_o != 0)))
    {
      /*printf ("#1.2 : (start : %ld)(PW_o:%ld)(sample:%ld)\n",PW_Start_Address,PW_o,PW_k);*/
      return BAD;
    }
    
    /* size */
    PW_j = (in_data[PW_Start_Address+4+PW_k*12]*256) + in_data[PW_Start_Address+5+PW_k*12];
    /* loop start */
    PW_l = (in_data[PW_Start_Address+8+PW_k*12]*256) + in_data[PW_Start_Address+9+PW_k*12];
    /* loop size */
    PW_m = (in_data[PW_Start_Address+10+PW_k*12]*256) + in_data[PW_Start_Address+11+PW_k*12];
    if ( (PW_l>PW_j) || (PW_m>PW_j+1) || (PW_j>32768))
    {
      /*printf ("#2 : (start : %ld) (smpsize:%ld)(loopstart:%ld)(loopsiz:%ld)\n",PW_Start_Address,PW_j,PW_l,PW_m);*/
      return BAD;
    }
    if ( PW_m == 0 )
    {
      /*printf ("#3 : (start : %ld)\n",PW_Start_Address);*/
      return BAD;
    }
    if ( (PW_j == 0) && ((PW_l != 0) || (PW_m != 1) ))
    {
      /*printf ("#4 : (start : %ld)\n",PW_Start_Address);*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if (PW_WholeSampleSize < 2)
  {
    /*printf ("#4.5 : (start : %ld)\n",PW_Start_Address);*/
    return BAD;
  }

  /* test pattern addresses */
  PW_o=BAD;
  PW_l = 0;
  for ( PW_k=0 ; PW_k<256; PW_k += 2 )
  {
    /*printf ("%x - ",in_data[PW_Start_Address+PW_k+188]);*/
    if ( (in_data[PW_Start_Address+PW_k+180] == 0xFF) && (in_data[PW_Start_Address+PW_k+181] == 0xFF) )
    {
      PW_o = GOOD;
      break;
    }
    PW_j = ((in_data[PW_Start_Address+PW_k+180]*256)+
            in_data[PW_Start_Address+PW_k+181]);
    if (PW_j < 180)
    {
      /*printf ("#5 : (start : %ld)(PW_j:%ld)(PW_k:%ld)\n",PW_Start_Address,PW_j,PW_k);*/
      return BAD;
    }
    if (PW_j>PW_l)
      PW_l = PW_j;
  }
  if (PW_o == BAD)
  {
    /*printf ("#6 : (start : %ld)\n",PW_Start_Address);*/
    return BAD;
  }

  /* PW_l is the max addy of the pattern addies */
  return GOOD;
}


/* With the help of Xigh :) .. thx */
int _cdecl cmplong(const void * a, const void * b)
{
  long * aa = (long *) a;
  long * bb = (long *) b;
  if (*aa == *bb)
    return 0;
  if(*aa > *bb)
    return +1;
  return -1;
}


void Rip_TitanicsPlayer ( void )
{
  /* retrieve highest smp addy */
  PW_j = 0;PW_n=0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    PW_o = ((in_data[PW_Start_Address+PW_k*12]*256*256*256) +
            (in_data[PW_Start_Address+1+PW_k*12]*256*256)+
            (in_data[PW_Start_Address+2+PW_k*12]*256)+
            in_data[PW_Start_Address+3+PW_k*12]);
    if ( PW_o > PW_n)
    {
      PW_n = PW_o;
      PW_j = PW_k;
    }
  }
  /* PW_n is the highest smp addy ; PW_j is its ref */
  OutputSize = PW_n + (((in_data[PW_Start_Address+PW_j*12+4]*256)+in_data[PW_Start_Address+PW_j*12+5])*2);

  CONVERT = GOOD;
  Save_Rip ( "Titanics Player module", TitanicsPlayer );
  
  if ( Save_Status == GOOD )
    PW_i += 180;  /* after smp desc */
}


/*
 *   TitanicsPlayer.c   2007 (c) Sylvain "Asle" Chipaux
 *
*/

#define ON  1
#define OFF 2

void Depack_TitanicsPlayer ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  long Pat_Addresses[128];
  long Pat_Addresses_ord[128];
  long Pat_Addresses_final[128];
  long Max=0l;
  Uchar poss[37][2];
  Uchar PatPos;
  long Where=PW_Start_Address;
  long SmpAddresses[15];
  long SampleSizes[15];
  unsigned long i=0,j=0,k=0,l;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Pat_Addresses , 128 );
  BZERO ( Pat_Addresses_ord , 128 );
  BZERO ( Pat_Addresses_final , 128 );

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.stk" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc ( 2048 );
  BZERO ( Whatever , 2048 );
  fwrite ( &Whatever[0] , 20 , 1 , out );

  /* read and write whole header */
  /*printf ( "Converting sample headers ... " );*/
  for ( i=0 ; i<15 ; i++ ) /* only 15 samples */
  {
    /* retrieve addresses of samples */
    SmpAddresses[i] = ((in_data[Where]*256*256*256)+
                     (in_data[Where+1]*256*256)+
                     (in_data[Where+2]*256)+
                     (in_data[Where+3]));

    /* write name */
    fwrite ( &Whatever[0] , 22 , 1 , out );
    /* size */
    k = (in_data[Where+4] * 256) + in_data[Where+5];
    k *= 2;
    SampleSizes[i] = k;
    fwrite ( &in_data[Where+4] , 1 , 1 , out );
    fwrite ( &in_data[Where+5] , 1 , 1 , out );
    /* finetune */
    fwrite ( &in_data[Where+6] , 1 , 1 , out );
    /* volume */
    fwrite ( &in_data[Where+7] , 1 , 1 , out);
    /* loop start */
    fwrite ( &in_data[Where+8] , 1 , 1 , out );
    fwrite ( &in_data[Where+9] , 1 , 1 , out );
    fwrite ( &in_data[Where+10] , 1 , 1 , out );
    fwrite ( &in_data[Where+11] , 1 , 1 , out );

    Where += 12;
  }
  /*printf ( "ok\n" );*/

  /* pattern list */
  /*printf ( "creating the pattern list ... " );*/
  for ( PatPos=0x00 ; PatPos<128 ; PatPos++ )
  {
    if ( in_data[Where+PatPos*2] == 0xFF )
      break;
    Pat_Addresses_ord[PatPos] = Pat_Addresses[PatPos] = (in_data[Where+PatPos*2]*256)+in_data[Where+PatPos*2+1];
  }

  /* write patpos */
  fwrite ( &PatPos , 1 , 1 , out );

  /* restart byte */
  c1 = 0x00;
  fwrite ( &c1 , 1 , 1 , out );

  /* With the help of Xigh :) .. thx */
  qsort (Pat_Addresses_ord,PatPos,sizeof(long),cmplong);
  j=0;
  for (i=0;i<PatPos;i++)
  {
    Pat_Addresses_final[j++] = Pat_Addresses_ord[i];
    while ((Pat_Addresses_ord[i+1] == Pat_Addresses_ord[i]) && (i<PatPos))
       i += 1;
  }

  /* write pattern list */
  for (i=0;i<PatPos;i++)
  {
    for (j=0;Pat_Addresses[i]!=Pat_Addresses_final[j];j++);
    Whatever[i] = j;
    if (j>Max)
      Max = j;
  }
  fwrite ( Whatever , 128 , 1 , out );
  /*printf ( "ok\n" );*/

  /* pattern data */
  for (i=0;i<=Max;i++)
  {
    k = 0;
    /*printf ("Where : %ld\n",Where);*/
    Where = Pat_Addresses_final[i] + PW_Start_Address;
    BZERO (Whatever,2048);
    do
    {
      /* k is row nbr */
      c1 = (((in_data[Where+1]>>6)&0x03)*4); /* voice */

      /* no note ... */
      if ((in_data[Where+1]&0x3f)<=36)
      {
        Whatever[(k*16)+c1] = poss[in_data[Where+1]&0x3f][0];
        Whatever[(k*16)+c1+1] = poss[in_data[Where+1]&0x3F][1];
      }
      Whatever[(k*16)+c1+2] = in_data[Where+2];
      Whatever[(k*16)+c1+3] = in_data[Where+3];
      Where += 4;
 
      if ((in_data[Where]&0x7f) != 0x00)
        k += (in_data[Where]&0x7f);
      if (k > 1024)
      {
        /*printf ("pat %ld too big\n",i);*/
        break;
      }
    }while ((in_data[Where-4] & 0x80) != 0x80); /* pattern break it seems */
    fwrite (&Whatever[0],1024,1,out);
  }
  Where -= 4;
  /* Where is now on the first smp */

  free ( Whatever );
  /*printf ( " ok\n" );*/
  /*fflush ( stdout );*/


  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  for (i=0; i<15; i++)
  {
    if ( SmpAddresses[i] != 0 )
      fwrite ( &in_data[PW_Start_Address+SmpAddresses[i]], SampleSizes[i], 1, out);
  }

  /* crap */
  Crap15 ( "  Titanics Player " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
