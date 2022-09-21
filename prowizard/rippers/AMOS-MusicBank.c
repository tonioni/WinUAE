/* (31 mar 2003) */
/* testAmBk()    */
/* Rip_AmBk()    */
/* Depack_AmBk() */

#include "globals.h"
#include "extern.h"


short testAmBk ( void )
{
  if (PW_i + 68 > PW_in_size)
  {
    return BAD;
  }

  /* test #1  */
  PW_Start_Address = PW_i;
  if ((in_data[PW_Start_Address+4] != 0x00)||
      (in_data[PW_Start_Address+5] != 0x03)||
      (in_data[PW_Start_Address+6] != 0x00)||
      (in_data[PW_Start_Address+7] >  0x01)||
      (in_data[PW_Start_Address+12]!= 'M')||
      (in_data[PW_Start_Address+13]!= 'u')||
      (in_data[PW_Start_Address+14]!= 's')||
      (in_data[PW_Start_Address+15]!= 'i')||
      (in_data[PW_Start_Address+16]!= 'c')||
      (in_data[PW_Start_Address+17]!= ' ')||
      (in_data[PW_Start_Address+18]!= ' ')||
      (in_data[PW_Start_Address+19]!= ' '))
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get the whole size */
  PW_k = (in_data[PW_Start_Address+9]*256*256)+(in_data[PW_Start_Address+10]*256)+in_data[PW_Start_Address+11]+12;
  if ( PW_k+PW_Start_Address > PW_in_size )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}


/* Rip_AmBk */
void Rip_AmBk ( void )
{
  /* PW_k is already the whole file size */

  OutputSize = PW_k;

  CONVERT = GOOD;
  Save_Rip ( "AMOS Bank (AmBk)", AmBk );
  
  if ( Save_Status == GOOD )
    PW_i += 4;
}


/*
 *   ambk.c   mar 2003 (c) Asle / ReDoX
 * 
 *  based in Kyz description ... thx !.
 *
*/
void Depack_AmBk ( void )
{
  /*Uchar c1,c2,c3,c4;*/
  Uchar *Whatever,*address;
  Uchar poss[37][2];
  /*Uchar Note,Smp,Fx,FxVal;*/
  long i,j,k;
  long Where = PW_Start_Address;
  long INST_HDATA_ADDY,SONGS_DATA_ADDY,PAT_DATA_ADDY;/*,INST_DATA_ADDY;*/
  long BANK_LEN;
  long smps_addys[31],smp_sizes[31];
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever,1024);

  BANK_LEN = ((in_data[Where + 0x09]*256*256)+
	      (in_data[Where + 0x0a]*256)+
	      in_data[Where + 0x0b]) + 0x0c;

  INST_HDATA_ADDY = ((in_data[Where + 0x14]*256*256*256)+
		     (in_data[Where + 0x15]*256*256)+
		     (in_data[Where + 0x16]*256)+
		     in_data[Where + 0x17]) + 0x14;
  SONGS_DATA_ADDY = ((in_data[Where + 0x18]*256*256*256)+
		     (in_data[Where + 0x19]*256*256)+
		     (in_data[Where + 0x1A]*256)+
		     in_data[Where + 0x1B]) + 0x14;
  PAT_DATA_ADDY = ((in_data[Where + 0x1C]*256*256*256)+
		   (in_data[Where + 0x1D]*256*256)+
		   (in_data[Where + 0x1E]*256)+
		   in_data[Where + 0x1F]) +0x14;

  /* title stuff */
  Where = PW_Start_Address + SONGS_DATA_ADDY;
  j = in_data[Where]*256 + in_data[Where+1]; /* number of songs */
  if ( j > 1 )
  {
    printf ( "\n!!! unsupported feature in depack_AmBk() - send this file to asle@free.fr !\n" );
    free(Whatever);
    return;
  }
  j = ((in_data[Where+2]*256*256*256)+(in_data[Where+3]*256*256)+(in_data[Where+4]*256)+in_data[Where+5]);
  fwrite ( &in_data[Where + j + 0x0c], 16, 1, out );
  fwrite ( Whatever, 4, 1, out );

  Where = PW_Start_Address + INST_HDATA_ADDY;
  /*printf ( "\naddy of instrument headers : %ld\n", Where );*/

  /* samples header + data */
  j = (in_data[Where]*256) + in_data[Where+1];  /* nbr of samples */
  /*printf ( "nbr of samples : %ld\n", j );*/
  Where += 2;
  for ( i=0 ; i<j ; i++ )
  {
    smps_addys[i] = ((in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3]);
    /*    printf ( "sample[%ld] : %ld\n", i,smps_addys[i]);*/
    /* sample name */
    fwrite ( &in_data[Where+16], 16, 1, out );
    fwrite ( Whatever, 6, 1, out ); /* pad */
    /* size */
    k = 0x0e;
    if ( (in_data[Where+ 0x0e] == 0x00) && (in_data[Where + 0x0f] == 0x00))
      k = 0x08;
    if ( (((in_data[Where+k]*256) + in_data[Where+k+1]) == 2 )||
	 (((in_data[Where+k]*256) + in_data[Where+k+1]) == 4 ))
      fwrite (&Whatever[0], 2, 1, out );
    else
    {
      fwrite (&in_data[Where+k], 2, 1, out );
      smp_sizes[i] = (in_data[Where+k]*256) + in_data[Where+k];
    }
    /* fine + vol */
    fwrite ( &in_data[Where + 0x0c], 2, 1, out );
    /* loop */
    k = (in_data[Where+ 0x05]*256*256) + (in_data[Where + 0x06]*256) + in_data[Where + 0x07];
    if ( k  < smps_addys[0] )
      fwrite (&Whatever[0], 2, 1, out );
    else
    {
      k -= smps_addys[i]; k/=2;
      /* PC only code !!! */
      address = (Uchar *) &k;
      Whatever[32] = *(address+1);
      Whatever[33] = *address;
      fwrite ( &Whatever[32], 2, 1, out );
    }

    /* loop size */
    if ( (in_data[Where + 0x0a] == 0x00) && (in_data[Where + 0x0b] <= 0x02)  )
    {
      Whatever[29] = 0x01;
      fwrite ( &Whatever[28], 2, 1, out );
    }
    else
      fwrite ( &in_data[Where + 0x0a], 2, 1, out );

    Where += 32;
    /*printf ( "where out : %x\n", ftell (out ));*/
  }
  /* padding to 31 samples */
  while (i++ < 31)
    fwrite ( Whatever, 30, 1, out );
  /* end of sample header */

  

  /* crap ... */
  /*  Crap ( "       AmBk       " , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
