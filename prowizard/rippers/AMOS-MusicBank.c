/* (31 mar 2003) */
/* testAmBk()    */
/* Rip_AmBk()    */
/* Depack_AmBk() */

#include "globals.h"
#include "extern.h"


int16_t testAmBk ( void )
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
  /*uint8_t c1,c2,c3,c4;*/
  uint8_t *Whatever,*address, *Header;
  uint8_t poss[37][2];
  /*uint8_t Note,Smp,Fx,FxVal;*/
  int32_t	 i,j,k;
  int32_t	 Where = PW_Start_Address;
  int32_t	 INST_HDATA_ADDY,SONGS_DATA_ADDY,PAT_DATA_ADDY;/*,INST_DATA_ADDY;*/
  int32_t	 BANK_LEN;
  int32_t	 smps_addys[31],smp_sizes[31];
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  fillPTKtable(poss);

  Whatever = (uint8_t *) malloc (1024);
  BZERO (Whatever,1024);
  Header = (uint8_t *) malloc (1084);
  BZERO (Header,1084);

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
    return;
  }
  j = ((in_data[Where+2]*256*256*256)+(in_data[Where+3]*256*256)+(in_data[Where+4]*256)+in_data[Where+5]);
  for (i=0; i<16; i++)
    Header[i] = in_data[Where + j + 0x0c + i];
  /*fwrite ( &in_data[Where + j + 0x0c], 16, 1, out );*/
  /*fwrite ( Whatever, 4, 1, out );*/

  Where = PW_Start_Address + INST_HDATA_ADDY;
  /*printf ( "\naddy of instrument headers : %ld\n", Where );*/

  /* samples header + data */
  j = (in_data[Where]*256) + in_data[Where+1];  /* nbr of samples */
  /*printf ( "nbr of samples : %ld\n", j );*/
  Where += 2;
  for ( i=0 ; i<j ; i++ )
  {
    int a;
    smps_addys[i] = ((in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3]);
    /*    printf ( "sample[%ld] : %ld\n", i,smps_addys[i]);*/
    /* sample name */
    for (a=0; a<16; a++)
      Header[20+(i*30)+a] = in_data[Where+16+a];

    /* size */
    k = 0x0e;
    if ( (in_data[Where+ 0x0e] == 0x00) && (in_data[Where + 0x0f] == 0x00))
      k = 0x08;

    Header[42+(i*30)] = in_data[Where+k];
    Header[43+(i*30)] = in_data[Where+k+1];
    smp_sizes[i] = (in_data[Where+k]*256) + in_data[Where+k];

    /* fine + vol */
    Header[44+(i*30)] = in_data[Where + 0x0c];
    Header[45+(i*30)] = in_data[Where + 0x0d];
    /*fwrite ( &in_data[Where + 0x0c], 2, 1, out );*/
    /* loop */
    k = (in_data[Where+ 0x05]*256*256) + (in_data[Where + 0x06]*256) + in_data[Where + 0x07];
    if (k>=smps_addys[i])
    {
      k -= smps_addys[i]; k/=2;
        /* PC only code !!! */
      address = (uint8_t *) &k;
      Header[46+(i*30)] = *(address+1);
      Header[47+(i*30)] = *address;
    }

    /* loop size */
    if ( (in_data[Where + 0x0a] == 0x00) && (in_data[Where + 0x0b] <= 0x02)  )
    {
      Header[49+(i*30)] = 0x01;
    }
    else
    {
      Header[48+(i*30)] = in_data[Where + 0x0a];
      Header[49+(i*30)] = in_data[Where + 0x0b];
    }

    Where += 32;
    /*printf ( "where out : %x\n", ftell (out ));*/
  }
  /* end of sample header */

  

  fwrite (Header, 1084, 1, out);




  free(Header);
  free(Whatever);

  /* crap ... */
  /*  Crap ( "       AmBk       " , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
