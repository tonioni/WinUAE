/* testSTRUGGLE() */
/* Rip_STRUGGLE() */
/* Depack_STRUGGLE() */

#include "globals.h"
#include "extern.h"


int16_t	 testSTRUGGLE ( void )
{
  int32_t	 samplesize;
  
  if (PW_i<3)
    return BAD;
  
  PW_Start_Address = PW_i-3;

  if ((PW_i + 180 + 12 + 0x38) > PW_in_size)
    return BAD;

  /* test samples */
  PW_n = 0;
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    /* fine */
    if ( in_data[PW_Start_Address+2+PW_k*12] != 0 )
      return BAD;
    /* volume */
    if ( in_data[PW_Start_Address+3+PW_k*12] > 0x40 )
      return BAD;
    /* size */
    samplesize = ((in_data[PW_Start_Address+PW_k*12] * 256) + in_data[PW_Start_Address+1+PW_k*12])*2;
    PW_WholeSampleSize += samplesize;
    /* lstart */
    PW_j = ((in_data[PW_Start_Address+4+PW_k*12] * 256) + in_data[PW_Start_Address+5+PW_k*12])*2;
    /* lsize */
    PW_o = ((in_data[PW_Start_Address+6+PW_k*12] * 256) + in_data[PW_Start_Address+7+PW_k*12])*2;
    /* sizes tests */
    if ((PW_j > samplesize+2) || (PW_o > samplesize+2 ) || ((PW_j + PW_o)>(samplesize*2)+2))
      return BAD;
    /* loop start not 0 while size 0*/
    if ((PW_j != 0) && (PW_o == 0))
      return BAD;
    /* loop start = loop size not 0 */
    if ((PW_j + PW_o) > samplesize)
      return BAD;
    /* addys */
    PW_m = ((in_data[PW_Start_Address+8+PW_k*12]*256*256*256) +
    (in_data[PW_Start_Address+9+PW_k*12]*256*256) +
    (in_data[PW_Start_Address+10+PW_k*12]*256) +
    in_data[PW_Start_Address+11+PW_k*12]);
    if ((PW_m < PW_n) || (PW_m < 0x100))
       return BAD;
    PW_n = PW_m;
  }

  /* pattern size */
  PW_j = in_data[PW_Start_Address+193];
  if ( (PW_j == 0) || (PW_j > 64) )
    return BAD;

  /* pattern list */
  PW_j = in_data[PW_Start_Address+193];
  for (PW_k = 0; PW_k<0x38; PW_k++)
  {
    if ( in_data[PW_Start_Address+194+PW_k] > 0x38 )
      return BAD;
  }

  /* whole size ok ? */
  if ( PW_WholeSampleSize < 4 )
     return BAD;

  return GOOD;
}


void Rip_STRUGGLE ( void )
{
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
    PW_WholeSampleSize += ((in_data[PW_Start_Address+PW_k*12]*256)+in_data[PW_Start_Address+1+PW_k*12])*2;
  
  PW_n = 0;
  for ( PW_k=0 ; PW_k<0x38 ; PW_k++ )
    if (in_data[PW_Start_Address+PW_k+0xC2] > PW_n)
      PW_n = in_data[PW_Start_Address+PW_k+0xC2];

  PW_n += 1; /* max pattern number */

  /* 0x100 header size */
  OutputSize = 0x100 + PW_WholeSampleSize + (PW_n*768);

  CONVERT = GOOD;
  Save_Rip ( "Struggle game module", STRUGGLE );
  
  if ( Save_Status == GOOD )
    PW_i += 3;
}


/*
 *   STRUGGLE.c   2008 (c) Sylvain "Asle" Chipaux
 *
 * example provided by Muerto, as usual :)
*/

#define ON  1
#define OFF 2

void Depack_STRUGGLE ( void )
{
  uint8_t *Whatever;
  int32_t	 Where=PW_Start_Address;
  uint32_t i=0,j=0,k=0;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (uint8_t *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write whole header */
  /*printf ( "Converting sample headers ... " );*/
  PW_WholeSampleSize = 0;
  for ( i=0 ; i<15 ; i++ )
  {
    /* write name */
    fwrite ( Whatever , 22 , 1 , out );
    /* size/finetune/volume/loops */
    PW_WholeSampleSize += ((in_data[Where] * 256) + in_data[Where+1])*2;
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 12;
  }

  /* bypassing 12 empty bytes (!) */
  Where += 12;

  /* pattern list */
  fwrite (&in_data[Where+1],1,1,out);
  fwrite (Whatever,1,1,out);
  Where += 2;
  /* 0x38 pos .. apparently */
  /* write pattern list */
  fwrite ( &in_data[Where] , 0x38 , 1 , out );
  /* complete to 128 */
  fwrite (Whatever,72,1,out);
  /* get highest pattern nbr */
  for (i=0;i<0x38;i++)
  {
    if (in_data[Where]>j)
      j = in_data[Where];
    Where ++;
  }
  /* j is the highest pattern number */

  /* bypass 6 empty bytes */
  Where += 6;

  /* pattern data */
  /*printf ( "Converting pattern datas (Where = %x, j = %x)",Where,j );*/
  for ( i=0 ; i<=j ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for (k=0;k<64*4;k++)
    {
      Whatever[k*4] = in_data[Where];
      Whatever[k*4+1] = in_data[Where+1];
      Whatever[k*4+2] = in_data[Where+2]<<4;
      Where += 3;
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/
  /*fflush ( stdout );*/


  /* sample data */
  /*printf ( "Saving sample data ... " );*/
  fwrite ( &in_data[Where] , PW_WholeSampleSize , 1 , out );


  /* crap */
  /*Crap ( "Struggle game music" , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
