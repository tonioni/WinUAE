/* (5th of may 2007)
   (1st of January 2010 - completed)
*/
/* testNovoTrade() */
/* Rip_NovoTrade() */
/* Depack_NovoTrade() */


#include "globals.h"
#include "extern.h"


int16_t	 testNovoTrade ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i;
  if ((PW_i + 38) > PW_in_size)
  {
    /*printf ("[1] PW_Start_Address : %ld\n", PW_Start_Address);*/
    return BAD;
  }
  PW_j = (in_data[PW_Start_Address+20]*256)+in_data[PW_Start_Address+21] + 4;
  if ((PW_i + PW_j + 4) > PW_in_size)
  {
    /*printf ("[2] PW_Start_Address : %ld\n", PW_Start_Address);*/
    return BAD;
  }
  PW_k = (in_data[PW_Start_Address+28]*256)+in_data[PW_Start_Address+29] + PW_j + 4;
  if ((PW_i + PW_k + 2) > PW_in_size)
  {
    /*printf ("[3] PW_Start_Address : %ld\n", PW_Start_Address);*/
    return BAD;
  }
  /* PW_j is on "BODY" tag */
  /* PW_k is on "SAMP" tag */

  /* test #2 let's verify */
  if ( (in_data[PW_Start_Address+PW_j] != 'B') &&
     (in_data[PW_Start_Address+PW_j+1] != 'O') &&
     (in_data[PW_Start_Address+PW_j+2] != 'D') &&
     (in_data[PW_Start_Address+PW_j+3] != 'Y'))
  {
    /*printf ("[4] PW_Start_Address : %ld\n", PW_Start_Address);*/
    return BAD;
  }
  if ( (in_data[PW_Start_Address+PW_k] != 'S') &&
     (in_data[PW_Start_Address+PW_k+1] != 'A') &&
     (in_data[PW_Start_Address+PW_k+2] != 'M') &&
     (in_data[PW_Start_Address+PW_k+3] != 'P'))
  {
    /*printf ("[5] (start)%ld, (BODY)%lx, (at)%lx\n", PW_Start_Address,PW_j,PW_k);*/
    return BAD;
  }

  /* no much but should be enough :) */
  return GOOD;
}


void Rip_NovoTrade ( void )
{
  /* get nbr sample */
  PW_j = (in_data[PW_Start_Address+22]*256)+in_data[PW_Start_Address+23];
  /* get BODY addy */
  PW_m = (in_data[PW_Start_Address+20]*256)+in_data[PW_Start_Address+21] + 4;
  /* get SAMP addy */
  PW_k = (in_data[PW_Start_Address+28]*256)+in_data[PW_Start_Address+29] + PW_m + 4;
  PW_WholeSampleSize = 0;
  for ( PW_l=0 ; PW_l<PW_j ; PW_l++ )
    PW_WholeSampleSize += ((in_data[32+(PW_l*8)]*256)+in_data[33+(PW_l*8)])*2;

  OutputSize = PW_WholeSampleSize + PW_k + 4;

  CONVERT = GOOD;
  Save_Rip ( "NovoTrade Packed music", NovoTrade );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}


/*
 *   NovoTrade.c   2007-2010 (c) Sylvain "Asle" Chipaux
 *
 * 20070505 : doesn't convert pattern data
 * 20100101 : Thanks to Claudio of XMP team. It's ok now.
*/
void Depack_NovoTrade ( void )
{
  uint8_t *Whatever, c1;
  int32_t	 i=0,k=0;
  uint16_t	 Pattern_Addresses_Table[128];
  int16_t	 BODYaddy, SAMPaddy, nbr_sample, siz_patlist, nbr_patstored;
  int32_t	 Total_Sample_Size=0;
  int32_t	 Where = PW_Start_Address;
  FILE *out;/*,*DEBUG;*/
  
  /*DEBUG = fopen("DEBUG.txt","w+b");*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Where += 4;
  /* title */
  Whatever = (uint8_t *) malloc (2048);
  BZERO ( Whatever , 2048 );
  fwrite ( &in_data[Where], 16, 1, out );
  fwrite ( Whatever, 4, 1, out);
  Where += 16;

  /* get 'BODY' addy */
  BODYaddy = (in_data[Where]*256)+in_data[Where+1] + 4;
  Where += 2;
  /*printf ( "addy of 'BODY' : %ld\n" , k );*/

  /* number of sample */
  nbr_sample = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  
  /* size of the pattern list */
  siz_patlist = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  
  /* number of pattern stored */
  nbr_patstored = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;

  /* get 'SAMP' addy */
  SAMPaddy = (in_data[Where]*256)+in_data[Where+1] + BODYaddy + 4;
  Where += 2;

  /* sample header */
  BZERO ( Whatever, 2048 );
  for ( i=0 ; i<nbr_sample ; i++ )
  {
    /* in_data[Where] is the sample ref */
    /* volume */
    Whatever[25+(in_data[Where]*30)] = in_data[Where+1];
    /* size */
    Whatever[22+(in_data[Where]*30)] = in_data[Where+2];
    Whatever[23+(in_data[Where]*30)] = in_data[Where+3];
    Total_Sample_Size += ((in_data[Where+2]*256)+in_data[Where+3])*2;
    /* loop start */
    Whatever[26+(in_data[Where]*30)] = in_data[Where+4];
    Whatever[27+(in_data[Where]*30)] = in_data[Where+5];
    /* loop size */
    Whatever[28+(in_data[Where]*30)] = in_data[Where+6];
    Whatever[29+(in_data[Where]*30)] = in_data[Where+7];
    Where += 8;
  }
  fwrite ( Whatever , 930 , 1 , out );

  /* pattern list now */
  /* Where is on it */
  BZERO ( Whatever, 2048 );
  for ( i=2; i<siz_patlist+2; i++ )
  {
    Whatever[i] = in_data[Where+1];
    Where += 2;
  }
  Whatever[0] = (uint8_t)siz_patlist;
  Whatever[1] = 0x7F;
  /* PTK's tag now*/
  Whatever[130] = 'M';
  Whatever[131] = '.';
  Whatever[132] = 'K';
  Whatever[133] = '.';
  fwrite ( Whatever , 134 , 1 , out );
  
  /* pattern addresses now */
  /* Where is on it */
  BZERO ( Pattern_Addresses_Table , 128*2 );
  for ( i=0; i<nbr_patstored; i++ )
  {
    Pattern_Addresses_Table[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
  }
  

/*fprintf(DEBUG,"BODYaddy : %d\n",BODYaddy);
fprintf(DEBUG,"SAMPaddy : %d\n",SAMPaddy);
fprintf(DEBUG,"nbr_sample : %d\n",nbr_sample);
fprintf(DEBUG,"siz_patlist : %d\n",siz_patlist);
fprintf(DEBUG,"nbr_patstored : %d\n\n",nbr_patstored);*/

  /* pattern data now ... *gee* */
  Where += 4;
  for ( i=0 ; i<nbr_patstored ; i++ )
/*  for ( i=0 ; i<2 ; i++ )*/
  {
    BZERO ( Whatever, 2048 );
/*fprintf(DEBUG,"\n-------pat %ld----\n",i);*/
    Where = BODYaddy + 4 + Pattern_Addresses_Table[i];
/*fprintf(DEBUG,"@ in file : %ld\n",Where);*/
    for ( k=0; k<64; k++ )
    {
      c1 = in_data[Where+1];
      if (c1 == 0x80)
      {
/*fprintf(DEBUG,"[%-2ld] %2x-%2x <-- end of pattern\n",k,in_data[Where],in_data[Where+1]);*/
        /* pattern ends */
        Where += 2;
        k += 1;
        break;
      }
      if (c1 == 0x00)
      {
/*fprintf(DEBUG,"[%-2ld] %2x-%2x <-- empty line\n",k,in_data[Where],in_data[Where+1]);*/
        /* empty line */
        Where += 2;
        continue;
      }
      if (c1 >0x0F) 
      {
/*fprintf(DEBUG,"[%-2ld] %2x-%2x <-- unknown case\n",k,in_data[Where],in_data[Where+1],in_data[Where+1]+1);*/
        /* bypass notes .. guess */
        Where += 2;
        continue;
      }
/*fprintf(DEBUG,"[%-2ld] %2x-%2x\n",k,in_data[Where],in_data[Where+1]);*/
      
      Where += 2;
      if ((c1 & 0x01) == 0x01)
      {
        Whatever[k*16] = in_data[Where];
        Whatever[k*16+1] = in_data[Where+1];
        Whatever[k*16+2] = in_data[Where+2];
        Whatever[k*16+3] = in_data[Where+3];
        Where += 4;
      }
      if ((c1 & 0x02) == 0x02)
      {
        Whatever[k*16+4] = in_data[Where];
        Whatever[k*16+5] = in_data[Where+1];
        Whatever[k*16+6] = in_data[Where+2];
        Whatever[k*16+7] = in_data[Where+3];
        Where += 4;
      }
      if ((c1 & 0x04) == 0x04)
      {
        Whatever[k*16+8] = in_data[Where];
        Whatever[k*16+9] = in_data[Where+1];
        Whatever[k*16+10] = in_data[Where+2];
        Whatever[k*16+11] = in_data[Where+3];
        Where += 4;
      }
      if ((c1 & 0x08) == 0x08)
      {
        Whatever[k*16+12] = in_data[Where];
        Whatever[k*16+13] = in_data[Where+1];
        Whatever[k*16+14] = in_data[Where+2];
        Whatever[k*16+15] = in_data[Where+3];
        Where += 4;
      }
/*fprintf(DEBUG,"[->] %2x%2x%2x%2x - %2x%2x%2x%2x - %2x%2x%2x%2x - %2x%2x%2x%2x\n"
             ,Whatever[k*16],Whatever[k*16+1],Whatever[k*16+2],Whatever[k*16+3]
             ,Whatever[k*16+4],Whatever[k*16+5],Whatever[k*16+6],Whatever[k*16+7]
             ,Whatever[k*16+8],Whatever[k*16+9],Whatever[k*16+10],Whatever[k*16+11]
             ,Whatever[k*16+12],Whatever[k*16+13],Whatever[k*16+14],Whatever[k*16+15]
             );*/
    }
/*fprintf(DEBUG,"\nEND OF LOOP ?!?\n");*/
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );


  Where = PW_Start_Address + SAMPaddy + 4;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " NovoTrade Packer " , BAD , BAD , out );

 /* fflush ( DEBUG );
  fclose ( DEBUG );*/
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */

}
