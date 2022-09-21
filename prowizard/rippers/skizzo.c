/* (5th of may 2007)
*/
/* testSkizzo() */
/* Rip_Skizzo() */
/* Depack_Skizzo() */


#include "globals.h"
#include "extern.h"


short testSkizzo ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i-24;
  if ((PW_i + 38) > PW_in_size)
  {
    /*printf ("[1] PW_Start_Address : %ld\n", PW_Start_Address);*/
    return BAD;
  }
  PW_j = in_data[PW_Start_Address+22]; /* nbr of samples */
  if ((PW_j > 31) || (PW_j==0))
  {
    /*printf ("[2] (nbr of samples:%ld) PW_Start_Address : %ld\n", PW_j,PW_Start_Address);*/
    return BAD;
  }

  return GOOD;
}


void Rip_Skizzo ( void )
{
  /* PW_j is the nbr of samples */
  /* get nbr of stored patts */
  PW_k = in_data[PW_Start_Address+23];
  /* get size of patlist */
  PW_m = in_data[PW_Start_Address+20] & 0x7f;
  PW_WholeSampleSize = 0;
  for ( PW_l=0 ; PW_l<PW_j ; PW_l++ )
    PW_WholeSampleSize += ((in_data[PW_Start_Address+28+(PW_l*8)]*256)+
                       in_data[PW_Start_Address+29+(PW_l*8)])*2;

  OutputSize = PW_WholeSampleSize + (PW_j*8) + (PW_k*768) + PW_m + 28;

  CONVERT = GOOD;
  Save_Rip ( "Skizzo Packed music", Skizzo );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}


/*
 *   Skizzo.c   2007 (c) Asle / ReDoX
 *
 * 20070525 : doesn't convert pattern data
*/
void Depack_Skizzo ( void )
{
  Uchar *Whatever;
  long i=0,k=0;
  short BODYaddy, SAMPaddy, nbr_sample, siz_patlist, nbr_patstored;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  Uchar poss[37][2];
  FILE *out,*DEBUG;

  /* filling up the possible PTK notes */
  fillPTKtable(poss);
  
/*  DEBUG = fopen("DEBUG.txt","w+b");*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc (2048);
  BZERO ( Whatever , 2048 );
  /* write title */
  fwrite ( &in_data[Where], 20, 1, out );
  Where += 20;

  /* read size of patternlist */
  siz_patlist = in_data[Where]; /* &0x7F necessary ! */
  Where += 2; /* with bypassing an unknown '00' */

  /* read number of sample */
  nbr_sample = in_data[Where];
  Where += 1;
  
  /* read nbr of pattern saved */
  nbr_patstored = in_data[Where];
  Where += 5; /* bypassing '-GD-' too */
  
  /* sample header */
  BZERO ( Whatever, 2048 );
  for ( i=0 ; i<nbr_sample ; i++ )
  {
    /* size */
    Whatever[22+(i*30)] = in_data[Where];
    Whatever[23+(i*30)] = in_data[Where+1];
    Total_Sample_Size += ((in_data[Where]*256)+in_data[Where+1])*2;
    /* finetune */
    Whatever[24+(i*30)] = in_data[Where+2];
    /* volume */
    Whatever[25+(i*30)] = in_data[Where+3];
    /* loop start */
    Whatever[26+(i*30)] = in_data[Where+4];
    Whatever[27+(i*30)] = in_data[Where+5];
    /* loop size */
    Whatever[28+(i*30)] = in_data[Where+6];
    Whatever[29+(i*30)] = in_data[Where+7];
    Where += 8;
  }
  /* finally, write the PTK header */
  fwrite ( Whatever , 930 , 1 , out );

  /* pattern list now */
  /* 'Where' is on it */
  BZERO ( Whatever, 2048 );
  for ( i=2; i<(siz_patlist&0x7f)+2; i++ )
  {
    Whatever[i] = in_data[Where];
    Where += 1;
  }
  Whatever[i+1] = (nbr_patstored - 1);
  Whatever[0] = (Uchar)(siz_patlist&0x7f);
  Whatever[1] = 0x7F;
  fwrite ( Whatever , 130 , 1 , out );
  
  /* PTK's tag now*/
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );
/*
fprintf(DEBUG,"nbr_sample : %d\n",nbr_sample);
fprintf(DEBUG,"siz_patlist : %d\n",siz_patlist&0x7f);
fprintf(DEBUG,"nbr_patstored : %d\n\n",nbr_patstored);
*/
  /* pattern data now ... */
  for ( i=0 ; i<nbr_patstored ; i++ )
/*  for ( i=0 ; i<2 ; i++ )*/
  {
/*fprintf(DEBUG,"\n-------pat %ld----\n",i);
fprintf(DEBUG,"@ in file : %ld\n",Where);*/
    for ( k=0; k<256; k++ ) /* loop on each note for this pattern */
    {
/*fprintf(DEBUG,"[%-4ld] %2x-%2x-%2x\n",k,in_data[Where],in_data[Where+1],in_data[Where+2]); */

      Whatever[k*4] = poss[in_data[Where]&0x7f][0];
      /*Whatever[k*4] &= in_data[Where]&0x80;*/ /* not sure .. never seen */
      Whatever[k*4+1] = poss[in_data[Where]&0x7f][1];
      Whatever[k*4+2] = in_data[Where+1];
      Whatever[k*4+3] = in_data[Where+2];
      Where += 3;
    }
/*fprintf(DEBUG,"\nEND OF LOOP ?!?\n");*/
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );


  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( "       Skizzo     " , BAD , BAD , out );

/*  fflush ( DEBUG );
  fclose ( DEBUG );*/
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */

}
