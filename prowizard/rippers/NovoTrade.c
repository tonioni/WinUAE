/* (5th of may 2007)
*/
/* testNovoTrade() */
/* Rip_NovoTrade() */
/* Depack_NovoTrade() */


#include "globals.h"
#include "extern.h"


short testNovoTrade ( void )
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
 *   NovoTrade.c   2007 (c) Asle / ReDoX
 *
 * 20070505 : doesn't convert pattern data
*/
void Depack_NovoTrade ( void )
{
  Uchar *Whatever;
  long i=0,k=0;
  Ushort Pattern_Addresses_Table[128];
  short BODYaddy, SAMPaddy, nbr_sample, siz_patlist, nbr_patstored;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;/*,*DEBUG;*/
  
  /*DEBUG = fopen("DEBUG.txt","w+b");*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Where += 4;
  /* title */
  Whatever = (Uchar *) malloc (2048);
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
  Whatever[0] = (Uchar)siz_patlist;
  Whatever[1] = 0x7F;
  fwrite ( Whatever , 130 , 1 , out );
  
  /* pattern addresses now */
  /* Where is on it */
  BZERO ( Pattern_Addresses_Table , 128*2 );
  for ( i=0; i<nbr_patstored; i++ )
  {
    Pattern_Addresses_Table[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
  }
  
  /* PTK's tag now*/
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

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
/*fprintf(DEBUG,"\n-------pat %ld----\n",i);*/
    Where = BODYaddy + 4 + Pattern_Addresses_Table[i];
/*fprintf(DEBUG,"@ in file : %ld\n",Where);*/
    for ( k=0; k<1024; k++ )
    {
      if (in_data[Where+1] == 0x80)
      {
/*fprintf(DEBUG,"[%-4ld] %2x-%2x <-- end of pattern\n",k,in_data[Where],in_data[Where+1]);/*
        /* pattern ends */
        Where += 2;
        k += 1;
        break;
      }
      if (in_data[Where+1] == 0x01)
      {
/*fprintf(DEBUG,"[%-4ld] %2x-%2x <-- ?!? unknown case\n",k,in_data[Where],in_data[Where+1]);*/
        /* pattern ends */
        Where += 2;
        k += 1;
        break;
      }
      if ((in_data[Where] == 0x00) && (in_data[Where+1]<=0x70)) 
      {
/*fprintf(DEBUG,"[%-4ld] %2x-%2x <-- bypass %ld notes\n",k,in_data[Where],in_data[Where+1],in_data[Where+1]+1);*/
        /* bypass notes .. guess */
        k += (in_data[Where+1]+1)-1;
        Where += 2;
        continue;
      }
/*fprintf(DEBUG,"[%-4ld] %2x-%2x-%2x-%2x\n",k,in_data[Where],in_data[Where+1],in_data[Where+2],in_data[Where+3]);*/
      Whatever[k*4] = in_data[Where];
      Whatever[k*4+1] = in_data[Where+1];
      Whatever[k*4+2] = in_data[Where+2];
      Whatever[k*4+3] = in_data[Where+3];
      k += 3;
      Where += 4;
    }
/*fprintf(DEBUG,"\nEND OF LOOP ?!?\n");*/
/*    fwrite ( Whatever , 1024 , 1 , out );*/
  }
  free ( Whatever );


  Where = PW_Start_Address + SAMPaddy;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " NovoTrade Packer " , BAD , BAD , out );

/*  fflush ( DEBUG );*/
/*  fclose ( DEBUG );*/
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */

}
