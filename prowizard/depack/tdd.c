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

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

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

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  BZERO ( SampleAddresses , 31*4 );
  BZERO ( SampleSizes , 31*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

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
