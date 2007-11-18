/* testTheDarkDemon() */
/* Rip_TheDarkDemon() */
/* Depack_TheDarkDemon() */


#include "globals.h"
#include "extern.h"


short testTheDarkDemon ( void )
{
  /* test #1 */
  if ( PW_i < 137 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  PW_Start_Address = PW_i-137;

  /* len of file */
  if ( (PW_Start_Address + 564) >= PW_in_size )
  {
/*printf ( "#1,1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }

  /* test #2 (volumes,sample addresses and whole sample size) */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* sample address */
    PW_k = (in_data[PW_Start_Address+PW_j*14+130]*256*256*256)
          +(in_data[PW_Start_Address+PW_j*14+131]*256*256)
          +(in_data[PW_Start_Address+PW_j*14+132]*256)
          + in_data[PW_Start_Address+PW_j*14+133];
    /* sample size */
    PW_l = (((in_data[PW_Start_Address+PW_j*14+134]*256)+in_data[PW_Start_Address+PW_j*14+135])*2);
    /* loop start address */
    PW_m = (in_data[PW_Start_Address+PW_j*14+138]*256*256*256)
          +(in_data[PW_Start_Address+PW_j*14+139]*256*256)
          +(in_data[PW_Start_Address+PW_j*14+140]*256)
          + in_data[PW_Start_Address+PW_j*14+141];
    /* loop size (replen) */
    PW_n = (((in_data[PW_Start_Address+PW_j*14+142]*256)+in_data[PW_Start_Address+PW_j*14+143])*2);

    /* volume > 40h ? */
    if ( in_data[PW_Start_Address+PW_j*14+137] > 0x40 )
    {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* loop start addy < sampl addy ? */
    if ( PW_m < PW_k )
    {
/*printf ( "#2,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* addy < 564 ? */
    if ( (PW_k < 564) || (PW_m < 564) )
    {
/*printf ( "#2,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }

    /* loop start > size ? */
    if ( (PW_m-PW_k) > PW_l )
    {
/*printf ( "#2,3 (start:%ld)(SmpAddy:%ld)(loopAddy:%ld)(Size:%ld)\n"
         , PW_Start_Address , PW_k , PW_m , PW_l );*/
      return BAD;
    }

    /* loop start+replen > size ? */
    if ( ((PW_m-PW_k)+PW_n) > (PW_l+2) )
    {
/*printf ( "#2,31 (start:%ld)(size:%ld)(loopstart:%ld)(replen:%ld)\n"
         , PW_Start_Address , PW_l , PW_m-PW_k , PW_n );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_l;
  }

  if ( (PW_WholeSampleSize <= 2) || (PW_WholeSampleSize>(31*65535)) )
  {
/*printf ( "#2,4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


  /* test #3 (addresses of pattern in file ... possible ?) */
  /* PW_WholeSampleSize is the whole sample size :) */
  if ( (PW_WholeSampleSize + 564) > PW_in_size )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test size of pattern list */
  if ( (in_data[PW_Start_Address] > 0x7f) || (in_data[PW_Start_Address]==0x00) )
  {
/*printf ( "#4 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_j+2] > 0x7f )
    {
/*printf ( "#4,01 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+PW_j+2] > PW_k )
      PW_k = in_data[PW_Start_Address+PW_j+2];
  }
  PW_k += 1;
  PW_k *= 1024;

  /* test end of pattern list */
  for ( PW_j=in_data[PW_Start_Address]+2 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+PW_j+2] != 0 )
    {
/*printf ( "#4,02 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  

  /* test if not out of file range */
  if ( (PW_Start_Address + PW_WholeSampleSize+564+PW_k) > PW_in_size )
  {
/*printf ( "#4,1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }


  /* PW_WholeSampleSize is the whole sample data size */
  /* PW_k is the whole pattern data size */
  /* test pattern data now ... */
  PW_l = PW_Start_Address+564+PW_WholeSampleSize;
  /* PW_l points on pattern data */
  for ( PW_j=0 ; PW_j<PW_k ; PW_j+=4 );
  {
    /* sample number > 31 ? */
    if ( in_data[PW_l+PW_j] > 0x1f )
    {
/*printf ( "#5,0 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* note > 0x48 (36*2) */
    if ( (in_data[PW_l+PW_j+1] > 0x48) || ((in_data[PW_l+PW_j+1]&0x01) == 0x01 ) )
    {
/*printf ( "#5,1 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=C and FxArg > 64 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0c)&&(in_data[PW_l+PW_j+3]>0x40) )
    {
/*printf ( "#5,2 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=D and FxArg > 64 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0d)&&(in_data[PW_l+PW_j+3]>0x40) )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    /* fx=B and FxArg > 127 ? */
    if ( ((in_data[PW_l+PW_j+2]&0x0f)==0x0b)&&(in_data[PW_l+PW_j+3]>0x7f) )
    {
/*printf ( "#5,3 (start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }



  /* let's get free another var .. */
  PW_WholeSampleSize += PW_k;

  return GOOD;
}



void Rip_TheDarkDemon ( void )
{
  /* PW_WholeSampleSize is the WholeSampleSize + pattern data size */

  /* 564 = header */
  OutputSize = PW_WholeSampleSize + 564;

  CONVERT = GOOD;
  Save_Rip ( "The Dark Demon module", TDD );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 140);  /* 137 should do but call it "just to be sure" :) */
}



/*
 *   TDD.c   1999 (c) Asle / ReDoX
 *
 * Converts TDD packed MODs back to PTK MODs
 *
 * Update : 6 apr 2003
 *  - removed fopen() func ... .
 * Update : 26 nov 2003
 *  - used htonl() so that use of addy is now portable on 68k archs
 *
*/

void Depack_TheDarkDemon ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar poss[37][2];
  Uchar *Whatever;
  Uchar Pattern[1024];
  Uchar PatMax=0x00;
  long i=0,j=0,k=0,z;
  long Whole_Sample_Size=0;
  long SampleAddresses[31];
  long SampleSizes[31];
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  BZERO ( SampleAddresses , 31*4 );
  BZERO ( SampleSizes , 31*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* write ptk header */
  Whatever = (Uchar *) malloc ( 1080 );
  BZERO (Whatever , 1080);
  fwrite ( Whatever , 1080 , 1 , out );

  /* read/write pattern list + size and ntk byte */
  fseek ( out , 950 , 0 );
  fwrite ( &in_data[Where] , 130 , 1 , out );
  PatMax = 0x00;
  for ( i=0 ; i<128 ; i++ )
    if ( in_data[Where+i+2] > PatMax )
      PatMax = in_data[Where+i+2];
  Where += 130;
/*  printf ( "highest pattern number : %x\n" , PatMax );*/


  /* sample descriptions */
/*  printf ( "sample sizes/addresses:" );*/
  for ( i=0 ; i<31 ; i++ )
  {
    fseek ( out , 42+(i*30) , 0 );
    /* sample address */
    SampleAddresses[i] = ((in_data[Where]*256*256*256)+
                          (in_data[Where+1]*256*256)+
                          (in_data[Where+2]*256)+
                           in_data[Where+3]);
    Where += 4;
    /* read/write size */
    Whole_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    SampleSizes[i] = (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where],2,1,out);
    Where += 2;
/*    printf ( "%5ld ,%ld" , SampleSizes[i] , SampleAddresses[i]);*/

    /* read/write finetune */
    /* read/write volume */
    fwrite ( &in_data[Where] , 2 , 1 , out );
    Where += 2;

    /* read loop start address */
    j = ((in_data[Where]*256*256*256)+
         (in_data[Where+1]*256*256)+
         (in_data[Where+2]*256)+
          in_data[Where+3]);
    Where += 4;
    j -= SampleAddresses[i];
    j /= 2;
    /* use of htonl() suggested by Xigh !.*/
    z = htonl(j);
    c1 = *((Uchar *)&z+2);
    c2 = *((Uchar *)&z+3);

    /* write loop start */
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );

    /* read/write replen */
    fwrite ( &in_data[Where],2,1,out);
    Where += 2;
  }
/*  printf ( "\nWhole sample size : %ld\n" , Whole_Sample_Size );*/


  /* bypass Samples datas */
  Where += Whole_Sample_Size;
/*  printf ( "address of the pattern data : %ld (%x)\n" , ftell ( in ) , ftell ( in ) );*/

  /* write ptk's ID string */
  fseek ( out , 0 , 2 );
  c1 = 'M';
  c2 = '.';
  c3 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );


  /* read/write pattern data */
  for ( i=0 ; i<=PatMax ; i++ )
  {
    BZERO ( Pattern , 1024 );
    for ( j=0 ; j<64 ; j++ )
    {
/*fprintf ( info , "at : %ld\n" , (ftell ( in )-1024+j*16) );*/
      for ( k=0 ; k<4 ; k++ )
      {
        /* fx arg */
        Pattern[j*16+k*4+3]  = in_data[Where+j*16+k*4+3];

        /* fx */
        Pattern[j*16+k*4+2]  = in_data[Where+j*16+k*4+2]&0x0f;

        /* smp */
        Pattern[j*16+k*4]    = in_data[Where+j*16+k*4]&0xf0;
        Pattern[j*16+k*4+2] |= (in_data[Where+j*16+k*4]<<4)&0xf0;

        /* note */
        Pattern[j*16+k*4]   |= poss[in_data[Where+j*16+k*4+1]/2][0];
/*fprintf ( info , "(P[0]:%x)" , Pattern[j*16+k*4] );*/
        Pattern[j*16+k*4+1]  = poss[in_data[Where+j*16+k*4+1]/2][1];
/*fprintf ( info , "%2x ,%2x ,%2x ,%2x |\n"
               , Whatever[j*16+k*4]
               , Whatever[j*16+k*4+1]
               , Whatever[j*16+k*4+2]
               , Whatever[j*16+k*4+3] );
*/
      }
    }
    fwrite ( Pattern , 1024 , 1 , out );
    Where += 1024;
  }


  /* Sample data */
/*  printf ( "samples:" );*/
  for ( i=0 ; i<31 ; i++ )
  {
    if ( SampleSizes[i] == 0l )
    {
/*      printf ( "-" );*/
      continue;
    }
    Where = PW_Start_Address + SampleAddresses[i];
    fwrite ( &in_data[Where] , SampleSizes[i] , 1 , out );
/*    printf ( "+" );*/
  }


  Crap ( "  The Dark Demon  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
