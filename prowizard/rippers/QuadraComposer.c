/* testQuadraComposer() */
/* Rip_QuadraComposer() */
/* Depack_QuadraComposer() */

#include "globals.h"
#include "extern.h"


short testQuadraComposer ( void )
{
  /* test #1 */
  if ( PW_i < 8 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  PW_Start_Address = PW_i-8;

  /* test #2 "FORM" & "EMIC" */
  if ( (in_data[PW_Start_Address]    != 'F') ||
       (in_data[PW_Start_Address+1]  != 'O') ||
       (in_data[PW_Start_Address+2]  != 'R') ||
       (in_data[PW_Start_Address+3]  != 'M') ||
       (in_data[PW_Start_Address+12] != 'E') ||
       (in_data[PW_Start_Address+13] != 'M') ||
       (in_data[PW_Start_Address+14] != 'I') ||
       (in_data[PW_Start_Address+15] != 'C') )
  {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test number of samples */
  PW_l = in_data[PW_Start_Address+63];
  if ( (PW_l == 0x00) || (PW_l > 0x20) )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}



void Rip_QuadraComposer ( void )
{
  PW_l = (in_data[PW_Start_Address+4]*256*256*256)+
         (in_data[PW_Start_Address+5]*256*256)+
         (in_data[PW_Start_Address+6]*256)+
          in_data[PW_Start_Address+7];

  OutputSize = PW_l + 8;

  CONVERT = GOOD;
  Save_Rip ( "Quadra Composer module", QuadraComposer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 9);  /* 8 should do but call it "just to be sure" :) */
}



/*
 *   QuadraComposer.c   1999 (c) Asle / ReDoX
 *
 * Converts QC MODs back to PTK MODs
 *
*/
void Depack_QuadraComposer ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00,c5=0x00;
  Uchar Pat_Pos;
  Uchar Pat_Max=0x00;
  Uchar Real_Pat_Max=0x00;
  Uchar *Whatever;
  /*Uchar Row[16];*/
  Uchar Pattern[1024];
  Uchar NbrSample=0x00;
  Uchar RealNbrSample=0x00;
  Uchar NbrRow[128];
  Uchar poss[37][2];    /* <------ Ptk's pitch table */
  long  SmpAddresses[32];
  long  SmpSizes[32];
  long  PatAddresses[128];
  long  i=0,j=0,k=0;
  long  Where = PW_Start_Address;
  FILE  *out;

  if ( Save_Status == BAD )
    return;

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );


  BZERO ( SmpAddresses , 32*4 );
  BZERO ( SmpSizes , 32*4 );
  BZERO ( PatAddresses , 128*4 );
  BZERO ( NbrRow , 128 );

  /* bypass ID's and chunk sizes */
  Where += 22;
  /*fseek ( in , 22 , 0 );*/

  /* read and write title */
  fwrite ( &in_data[Where], 20, 1, out );
  /*for ( i=0 ; i<20 ; i++ )
  {
    fread ( &c1 , 1 , 1 , in );
    fwrite ( &c1 , 1 , 1 , out );
    }*/

  /* bypass composer and tempo */
  Where += 41; /* + title */
  /*fseek ( in , 21 , 1 );*/

  /* read number of samples */
  NbrSample = in_data[Where];
  Where += 1;
  /*fread ( &NbrSample , 1 , 1 , in );*/

  /* write empty 930 sample header */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  Whatever[29] = 0x01;
  for ( i=0 ; i<31 ; i++ )
    fwrite ( &Whatever[0],30,1,out);

  /* read and write sample descriptions */
/*printf ( "sample number:" );*/
  for ( i=0 ; i<NbrSample ; i++ )
  {
    fseek ( out ,20+(in_data[Where]-1)*30 , 0 );
    /* read sample number byte */
    if ( in_data[Where] > RealNbrSample )
      RealNbrSample = in_data[Where];

    /* read/write sample name */
    fwrite ( &in_data[Where+4],20,1,out );
    fwrite ( Whatever,2,1,out ); /* filling */

    /* write size */
    fwrite ( &in_data[Where+2] , 2 , 1 , out );
    /* store size */
    SmpSizes[in_data[Where]] = (((in_data[Where+2]*256)+in_data[Where+3])*2);

    /* finetune */
    fwrite ( &in_data[Where+25], 1, 1, out );

    /* write volume */
    fwrite ( &in_data[Where+1] , 1 , 1 , out );

    /* loops (start & len) */
    fwrite ( &in_data[Where+26],2,1,out);
    if ( (in_data[Where+28] != 0x00) || (in_data[Where+29] != 0x00) )
      fwrite ( &in_data[Where+28],2,1,out);
    else
      fwrite ( &Whatever[28],2,1,out);

    /* read address of this sample in the file */
    SmpAddresses[in_data[Where]] =( (in_data[Where+30]*256*256*256) +
                        (in_data[Where+31]*256*256) +
                        (in_data[Where+32]*256) +
                        (in_data[Where+33]) );
    Where += 34;
  }
/*printf ( "\n" );*/
  fseek ( out , 0 , 2 );


  /* patterns now */
  /* bypass "pad" ?!? */
  /*fread ( &c1 , 1 , 1 , in );*/
  if ( in_data[Where] == 0x00 )
    /*fseek ( in , -1 , 1 );*/
    Where += 1;

  /* read number of pattern */
  Pat_Max = in_data[Where++];
  /*fread ( &Pat_Max , 1 , 1 , in );*/
/*  printf ( "\nPat_Max : %d (at %x)\n" , Pat_Max , ftell ( in ) );*/

  /* read patterns info */
/*printf ( "pattern numbers:" );*/
  for ( i=0 ; i<Pat_Max ; i++ )
  {
    /* read pattern number */
    c5 = in_data[Where++];
    /*fread ( &c5 , 1 , 1 , in );*/
/*printf ("%d," , c5);*/
    /* read number of rows for each pattern */
    NbrRow[c5] = in_data[Where++];
    /*fread ( &NbrRow[c5] , 1 , 1 , in );*/

    /* bypass pattern name */
    Where += 20;
    /*fseek ( in , 20 , 1 );*/

    /* read pattern address */
    /*fread ( &c1 , 1 , 1 , in );
    fread ( &c2 , 1 , 1 , in );
    fread ( &c3 , 1 , 1 , in );
    fread ( &c4 , 1 , 1 , in );*/
    PatAddresses[c5] = ( (in_data[Where]*256*256*256) +
                        (in_data[Where+1]*256*256) +
                        (in_data[Where+2]*256) +
                        (in_data[Where+3]) );
    Where += 4;
  }
/*printf ("\n");*/


  /* pattern list */
  /* bypass "pad" ?!? */
  /*fread ( &c1 , 1 , 1 , in );*/
  if ( in_data[Where] == 0x00 )
    /*fseek ( in , -1 , 1 );*/
    Where += 1;

  /* read/write number of position */
  Pat_Pos = in_data[Where++];
  /*fread ( &Pat_Pos , 1 , 1 , in );*/
  fwrite ( &Pat_Pos , 1 , 1 , out );

  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* read/write pattern list */
  for ( i=0 ; i<Pat_Pos ; i++ )
  {
    /*fread ( &c1 , 1 , 1 , in );
      fwrite ( &c1 , 1 , 1 , out );*/
    fwrite ( &in_data[Where],1,1,out);
    if ( in_data[Where] > Real_Pat_Max )
      Real_Pat_Max = in_data[Where];
    Where += 1;
  }
/*printf ( "Real_Pat_Max : %d\n" , Real_Pat_Max );*/
  /* fill up to 128 */
  BZERO (Whatever,930);
  fwrite ( Whatever , 128 , 1 , out );

  /* write ptk's ID */
  c1 = 'M';
  c2 = '.';
  c3 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  fwrite ( &c3 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );


  /* pattern data */
/*printf ( "patterns('-'=empty):" );*/
  for ( i=0 ; i<=Real_Pat_Max ; i++ )
  {
    BZERO ( Pattern , 1024 );
    if ( PatAddresses[i] == 0l )
    {
/*printf ( "-(%d)" , NbrRow[i] );*/
      fwrite ( Pattern , 1024 , 1 , out );
      /*      printf ( "-" );*/
      continue;
    }
/*printf ( "#" );*/
    Where = PW_Start_Address + PatAddresses[i];
    /*fseek ( in , PatAddresses[i] , 0 );*/
    for ( j=0 ; j<=NbrRow[i] ; j++ )
    {
      /*BZERO ( Row , 16 );*/
      /*fread ( Row , 16 , 1 , in );*/
      for ( k=0 ; k<4 ; k++ )
      {
        /* Fx */
        /*Pattern[j*16+k*4+2]  =  Row[k*4+2];*/
        Pattern[j*16+k*4+2]  =  in_data[Where+k*4+2];

        /* Fx args */
        switch ( Pattern[j*16+k*4+2] )
        {
          case 0x09:
            /*printf ( "#" );*/
            /*Pattern[j*16+k*4+3]  =  (Row[k*4+3]*2);*/
            Pattern[j*16+k*4+3]  =  (in_data[Where+k*4+3]*2);
            break;
          case 0x0b:
            /*printf ( "!" );*/
            /*c4 = Row[k*4+3]%10;
	      c3 = Row[k*4+3]/10;*/
            c4 = in_data[Where+k*4+3]%10;
            c3 = in_data[Where+k*4+3]/10;
            Pattern[j*16+k*4+3] = 16;
            Pattern[j*16+k*4+3] *= c3;
            Pattern[j*16+k*4+3] += c4;
            break;
          case 0x0E:
            /*if ( (Row[k*4+3]&0xf0) == 0xf0 )*/
            if ( (in_data[Where+k*4+3]&0xf0) == 0xf0 )
              /*Pattern[j*16+k*4+3] = (Row[k*4+3]-0x10);*/
              Pattern[j*16+k*4+3] = (in_data[Where+k*4+3]-0x10);
            break;
          default:
            /*Pattern[j*16+k*4+3]  =  Row[k*4+3];*/
            Pattern[j*16+k*4+3]  =  in_data[Where+k*4+3];
        }

        /* smp nbr (4 lower bits) */
        /*Pattern[j*16+k*4+2]  |= ((Row[k*4]<<4)&0xf0);*/
        Pattern[j*16+k*4+2]  |= ((in_data[Where+k*4]<<4)&0xf0);
        /* notes */
        /*c1 = Row[k*4+1];*/
        c1 = in_data[Where+k*4+1];
        if ( c1 != 0xff )
        {
          Pattern[j*16+k*4]    =  poss[c1][0];
          Pattern[j*16+k*4+1]  =  poss[c1][1];
        }
        /* smp nbr (4 higher bits) */
        /*Pattern[j*16+k*4]    |= (Row[k*4]&0xf0);*/
        Pattern[j*16+k*4]    |= (in_data[Where+k*4]&0xf0);
      }
      Where += 16;
    }
    fwrite ( Pattern , 1024 , 1 , out );
  }

  /* sample data */
/*printf ( "\nsamples('-'=empty):" );*/
  for ( i=1 ; i<=RealNbrSample ; i++ )
  {
    if ( SmpSizes[i] == 0 )
    {
/*printf ( "-(%ld)" , SmpSizes[i] );*/
      continue;
    }
/*printf ( "#" );*/
    Where = PW_Start_Address + SmpAddresses[i];
    fwrite ( &in_data[Where] , SmpSizes[i] , 1 , out );
  }
/*printf ( "\n" );*/


/*  printf ( "\nwhere: %ld\n" , ftell ( in ) );*/


  /* crap */
  Crap ( " Quadra Composer  " , BAD , BAD , out );


  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
