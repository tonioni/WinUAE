/*
 *   TrackerPacker_v3.c   1998 (c) Asle / ReDoX
 *
 * Converts TP3 packed MODs back to PTK MODs
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 *
 * 28 November 1999 : Update
 *   - Some Optimizing for Speed and for Size.
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_TP3 ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar poss[37][2];
  Uchar *Whatever;
  Uchar Note,Smp,Fx,FxVal;
  Uchar PatMax=0x00;
  long Track_Address[128][4];
  long i=0,j=0,k;
  long Start_Pat_Address=999999l;
  long Whole_Sample_Size=0;
  long Max_Track_Address=0;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Address , 128*4*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* title */
  Where += 8;
  fwrite ( &in_data[Where] , 20 , 1 , out );
  Where += 20;

  /* number of sample */
  j = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  j /= 8;
  /*printf ( "number of sample : %ld\n" , j );*/

  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  for ( i=0 ; i<j ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );

    /* size */
    Whole_Sample_Size += (((in_data[Where+2]*256)+in_data[Where+3])*2);
    fwrite ( &in_data[Where+2] , 2 , 1 , out );

    /* write finetune & Volume */
    fwrite ( &in_data[Where] , 2 , 1 , out );

    /* loop start & Loop size */
    fwrite ( &in_data[Where+4] , 4 , 1 , out );

    Where += 8;
  }
  Whatever[29] = 0x01;
  while ( i!=31 )
  {
    fwrite ( Whatever , 30 , 1 , out );
    i++;
  }
  /*printf ( "Whole sample size : %ld\n" , Whole_Sample_Size );*/

  /* read size of pattern table */
  Where += 1;
  Whatever[256] = in_data[Where++]; /* PatPos*/
  fwrite ( &Whatever[256] , 1 , 1 , out );

  /* ntk byte */
  Whatever[0] = 0x7f;
  fwrite ( &Whatever[0] , 1 , 1 , out );

  for ( i=0 ; i<Whatever[256] ; i++ )
  {
    Whatever[i] = ((in_data[Where]*256)+in_data[Where+1])/8;
    Where += 2;
    if ( Whatever[i] > PatMax )
      PatMax = Whatever[i];
/*fprintf ( info , "%3ld: %ld\n" , i,Pats_Address[i] );*/
  }

  /* read tracks addresses */
  /* bypass 4 bytes or not ?!? */
  /* Here, I choose not :) */
/*fprintf ( info , "track addresses :\n" );*/
  for ( i=0 ; i<=PatMax ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      Track_Address[i][j] = (in_data[Where]*256)+in_data[Where+1];
      Where += 2;
      if ( Track_Address[i][j] > Max_Track_Address )
        Max_Track_Address = Track_Address[i][j];
/*fprintf ( info , "%6ld, " , Track_Address[i][j] );*/
    }
/*fprintf ( info , "  (%x)\n" , Max_Track_Address );*/
  }

  /*printf ( "Highest pattern number : %d\n" , PatMax );*/

  /* write pattern list */
  fwrite ( Whatever , 128 , 1 , out );


  /* ID string */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  Start_Pat_Address = Where + 2;
  /*printf ( "address of the first pattern : %ld\n" , Start_Pat_Address );*/
/*fprintf ( info , "address of the first pattern : %x\n" , Start_Pat_Address );*/

  /* pattern datas */
  /*printf ( "converting pattern data " );*/
  for ( i=0 ; i<=PatMax ; i++ )
  {
/*fprintf ( info , "\npattern %ld:\n\n" , i );*/
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
/*fprintf ( info , "track %ld: (at %ld)\n" , j , Track_Address[i][j]+Start_Pat_Address );*/
      Where = Track_Address[i][j]+Start_Pat_Address;
      for ( k=0 ; k<64 ; k++ )
      {
        c1 = in_data[Where++];
/*fprintf ( info , "%ld: %2x," , k , c1 );*/
        if ( (c1&0xC0) == 0xC0 )
        {
/*fprintf ( info , " <--- %d empty lines\n" , (0x100-c1) );*/
          k += (0x100-c1);
          k -= 1;
          continue;
        }
        if ( (c1&0xC0) == 0x80 )
        {
          c2 = in_data[Where++];
/*fprintf ( info , "%2x ,\n" , c2 );*/
          Fx    = (c1>>1)&0x0f;
          FxVal = c2;
          if ( (Fx==0x05) || (Fx==0x06) || (Fx==0x0A) )
          {
            if ( FxVal > 0x80 )
              FxVal = 0x100-FxVal;
            else if ( FxVal <= 0x80 )
              FxVal = (FxVal<<4)&0xf0;
          }
          if ( Fx == 0x08 )
            Fx = 0x00;
          Whatever[k*16+j*4+2]  = Fx;
          Whatever[k*16+j*4+3]  = FxVal;
          continue;
        }

        c2 = in_data[Where++];
/*fprintf ( info , "%2x, " , c2 );*/
        Smp   = ((c2>>4)&0x0f) | ((c1>>2)&0x10);
        if ( (c1&0x40) == 0x40 )
          Note = 0x7f-c1;
        else
          Note  = c1&0x3F;
        Fx    = c2&0x0F;
        if ( Fx == 0x00 )
        {
/*fprintf ( info , " <--- No FX !!\n" );*/
          Whatever[k*16+j*4] = Smp&0xf0;
          Whatever[k*16+j*4]   |= poss[Note][0];
          Whatever[k*16+j*4+1]  = poss[Note][1];
          Whatever[k*16+j*4+2]  = (Smp<<4)&0xf0;
          Whatever[k*16+j*4+2] |= Fx;
          continue;
        }
        c3 = in_data[Where++];
/*fprintf ( info , "%2x\n" , c3 );*/
        if ( Fx == 0x08 )
          Fx = 0x00;
        FxVal = c3;
        if ( (Fx==0x05) || (Fx==0x06) || (Fx==0x0A) )
        {
          if ( FxVal > 0x80 )
            FxVal = 0x100-FxVal;
          else if ( FxVal <= 0x80 )
            FxVal = (FxVal<<4)&0xf0;
        }

        Whatever[k*16+j*4] = Smp&0xf0;
        Whatever[k*16+j*4]   |= poss[Note][0];
        Whatever[k*16+j*4+1]  = poss[Note][1];
        Whatever[k*16+j*4+2]  = (Smp<<4)&0xf0;
        Whatever[k*16+j*4+2] |= Fx;
        Whatever[k*16+j*4+3]  = FxVal;
      }
      if ( Where > Max_Track_Address )
        Max_Track_Address = Where;
/*fprintf ( info , "%6ld, " , Max_Track_Address );*/
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "." );*/
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/

  /*printf ( "sample data address : %ld\n" , Max_Track_Address );*/

  /* Sample data */
  if ( ((Max_Track_Address/2)*2) != Max_Track_Address )
    Max_Track_Address += 1;
  fwrite ( &in_data[Max_Track_Address] , Whole_Sample_Size , 1 , out );

  Crap ( " Tracker Packer 3 " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , "[Converted with    ]" );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , "[TrackerPacker v3  ]" );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "[to Protracker     ]" );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , "[by Asle /ReDoX    ]" );
  */
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
