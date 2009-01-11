/*
 * SGT-Packer.c
 * 2006 (C) Sylvain "Asle" Chipaux
*/
/* testSGT() */
/* Rip_SGT() */
/* Depack_SGT() */


#include "globals.h"
#include "extern.h"

/* rudimentary tests .. shall add others */
short testSGT ( void )
{
  /* test 1 */
  if ( (PW_i<43) || ((PW_Start_Address+43)>PW_in_size) )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-43;
  PW_k = in_data[PW_Start_Address+30];
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    /* volume > 64 ? */
    if ( in_data[PW_Start_Address+43+PW_j*16] > 0x40 )
    {
      return BAD;
    }
    /* pad val != 00h ? */
    if ( in_data[PW_Start_Address+42+PW_j*16] != 0x00 )
    {
      return BAD;
    }
  }

  return GOOD;
}



/**/
void Rip_SGT ( void )
{
  /* PW_k = number of samples, but we don't care. */

  /* get whole sample size */
  PW_WholeSampleSize = ((in_data[PW_Start_Address+5]*256*256)+
			(in_data[PW_Start_Address+6]*256)+
			in_data[PW_Start_Address+7]);
  /* get add of sample data */
  PW_k = ((in_data[PW_Start_Address+1]*256*256)+
	  (in_data[PW_Start_Address+2]*256)+
	  in_data[PW_Start_Address+3]);

  OutputSize = PW_k + PW_WholeSampleSize;
  
  CONVERT = GOOD;
  Save_Rip ( "     SGT-Packer    ", SGTPacker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 44);  /* 43 should do but call it "just to be sure" :) */
}


/*
 *   SGT-Packer.c   2006 (c) Asle
 *
*/

void Depack_SGT ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00;
  Uchar poss[37][2];
  Uchar Max=0x00;
  Uchar Note,Smp,Fx,FxVal;
  Uchar PatternTableSize=0x00;
  Uchar NbrPat;
  long i=0,j=0,l=0,z;
  long WholeSampleSize;
  long SmpAddy,PatlistAddy,PatdataAddy;
  long SmpAddies[31], SmpSizes[31];
  long NbrSmp;
  long Where = PW_Start_Address;
  FILE *out; /*,*debug;*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  WholeSampleSize = ((in_data[Where+5]*256*256)+
		     (in_data[Where+6]*256)+
		     in_data[Where+7]);
  SmpAddy = ((in_data[Where]*256*256*256)+
	     (in_data[Where+1]*256*256)+
	     (in_data[Where+2]*256)+
	     in_data[Where+3])+8+Where;
  PatlistAddy = ((in_data[Where+20]*256*256*256)+
		 (in_data[Where+21]*256*256)+
		 (in_data[Where+22]*256)+
		 in_data[Where+23])+8+Where;
  PatdataAddy = ((in_data[Where+24]*256*256*256)+
		 (in_data[Where+25]*256*256)+
		 (in_data[Where+26]*256)+
		 in_data[Where+27])+8+Where;
  PatternTableSize = in_data[Where+28];
  NbrPat = in_data[Where+31];
  NbrSmp = in_data[Where+30];


/*  debug = fopen ( "debug" , "w+b" );*/

  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever , 1024);
  /* title */
  fwrite ( &Whatever[0] , 20 , 1 , out );

  /* read/write sample headers */
  Where += 32;
  for ( i=0 ; i<NbrSmp ; i++ )
  {
    /* read smp start address */
    j=(in_data[Where]*256*256*256)+
      (in_data[Where+1]*256*256)+
      (in_data[Where+2]*256)+
       in_data[Where+3];
    SmpAddies[i] = j;

    /* read lstart address */
    l=(in_data[Where+4]*256*256*256)+
      (in_data[Where+5]*256*256)+
      (in_data[Where+6]*256)+
       in_data[Where+7];


    /* read & write sample size */
    Whatever[22] = in_data[Where+12];
    Whatever[23] = in_data[Where+13];
    SmpSizes[i] = ((Whatever[22]*256)+Whatever[23])*2;
    //fwrite ( &in_data[Where+12] , 2 , 1 , out );

    /* calculate loop start value */
    j = j-l;

    /* write fine & vol */
    Whatever[24] = in_data[Where+10];
    Whatever[25] = in_data[Where+11];
    //fwrite ( &in_data[Where] , 2 , 1 , out );

    /* write loop start */
    /* use of htonl() suggested by Xigh !.*/
    j/=2;
    z = htonl(j);
    Whatever[26] = *((Uchar *)&z+2);
    Whatever[27] = *((Uchar *)&z+3);
    //fwrite ( Whatever , 2 , 1 , out );

    /* write loop size */
    Whatever[28] = in_data[Where+8];
    Whatever[29] = in_data[Where+9];
    //fwrite ( &in_data[Where+6] , 2 , 1 , out );

    fwrite ( &Whatever[0] , 30 , 1 , out );
    BZERO (Whatever , 1024);

    Where += 16;
  }

  /* OK, sample headers are done */

  /* number of pattern in pattern list */
  /* Noisetracker restart byte */
  Where = PatlistAddy;
  Whatever[0] = PatternTableSize;
  Whatever[1] = 0x7f;
  fwrite ( &Whatever[0] , 2 , 1 , out );


  /* patternlist now TODO !!!!*/
  /* pattern table (read,count and write) */
  for ( i=0 ; i<PatternTableSize ; i++ )
  {
    /* get addy of first pointer of each pattern */
    /* and convert it to char for storage in patternlist */
    Whatever[i] = (Uchar)(((in_data[Where+(i*256)]*256*256*256)+
			  (in_data[Where+(i*256)+1]*256*256)+
			  (in_data[Where+(i*256)+2]*256)+
			  in_data[Where+(i*256)+3])/256);
  }
  Whatever[128] = 'M';
  Whatever[129] = '.';
  Whatever[130] = 'K';
  Whatever[131] = '.';
  Whatever[132] = 0x00;
  fwrite ( &Whatever[0] , 132 , 1 , out );

  /* ok, patternlist written */
  /* pattern data, now */
  Where = PatdataAddy;

  for ( i=0 ; i<=NbrPat ; i++ )
  {
    BZERO ( Whatever , 1024 );

    for ( j=0 ; j<256 ; j+=4 )
    {
      /* TODO : depacking */
      Whatever[j*4] = in_data[Where+(i*1024)+(j*4)];
      Whatever[j*4+1] = in_data[Where+(i*1024)+(j*4)+1];
      Whatever[j*4+2] = in_data[Where+(i*1024)+(j*4)+2];
      Whatever[j*4+3] = in_data[Where+(i*1024)+(j*4)+3];
    }
    fwrite (&Whatever[0],1024,1,out);
  }

  free ( Whatever );

  /* sample data */
  /* no packing or delta detected, yet */
  for (i=0; i<NbrSmp; i++)
  {
    fwrite ( &in_data[SmpAddy+SmpAddies[i]] , SmpSizes[i] , 1 , out );
  }

  Crap ( "    SGT-Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
/*  fclose ( debug );*/

  printf ( "done\n" );
  return; /* useless ... but */
}

