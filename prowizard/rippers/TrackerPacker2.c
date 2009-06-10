/* testTP2() */
/* Rip_TP2() */
/* Depack_TP2() */


/*
 * bug correction in the lqst test ...
 *  (Thomas Neumann again ...)
*/

#include "globals.h"
#include "extern.h"


short testTP2 ( void )
{
  if ( (in_data[PW_i+4] != '_') ||
       (in_data[PW_i+5] != 'T') ||
       (in_data[PW_i+6] != 'P') ||
       (in_data[PW_i+7] != '2') )
  {
    /*printf ( "#1 Start: %ld\n" , PW_i );*/
    return BAD;
  }

  PW_Start_Address = PW_i;

  /* number of sample */
  PW_l = ( (in_data[PW_Start_Address+28]*256)+
	   in_data[PW_Start_Address+29] );
  if ( (((PW_l/8)*8) != PW_l) || (PW_l == 0) )
  {
    /*printf ( "#2 Start: %ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_l /= 8;
  /* PW_l is the number of sample */

  /* test finetunes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+30+PW_k*8] > 0x0f )
    {
      /*printf ( "#3 Start: %ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+31+PW_k*8] > 0x40 )
    {
      /*printf ( "#4 Start: %ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+PW_k*8+32]*256)+in_data[PW_Start_Address+PW_k*8+33];
    PW_m = (in_data[PW_Start_Address+PW_k*8+34]*256)+in_data[PW_Start_Address+PW_k*8+35];
    PW_n = (in_data[PW_Start_Address+PW_k*8+36]*256)+in_data[PW_Start_Address+PW_k*8+37];
    PW_j *= 2;
    PW_m *= 2;
    PW_n *= 2;
    if ( (PW_j > 0xFFFF) ||
         (PW_m > 0xFFFF) ||
         (PW_n > 0xFFFF) )
    {
      /*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m + PW_n) > (PW_j+2) )
    {
      /*printf ( "#5,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( (PW_m != 0) && (PW_n <= 2) )
    {
      /*printf ( "#5,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += PW_j;
  }
  if ( PW_WholeSampleSize <= 4 )
  {
    /*printf ( "#5,3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* pattern list size */
  PW_j = in_data[PW_Start_Address+PW_l*8+31];
  if ( (PW_j==0) || (PW_j>128) )
  {
    /*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_j is the size of the pattern list */
  /* PW_l is the number of sample */
  /* PW_WholeSampleSize is the sample data size */
  return GOOD;
}




void Rip_TP2 ( void )
{
  /* PW_j is the size of the pattern list */
  /* PW_l is the number of sample */
  /* PW_WholeSampleSize is the sample data size */


  PW_m=0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_l*8+32+PW_k*2]*256)+in_data[PW_Start_Address+PW_l*8+33+PW_k*2];
    if ( PW_o > PW_m )
      PW_m = PW_o;
  }
  /* PW_m is the highest pattern number */
  PW_m += 8;
  /* PW_m is now the size of the track table list */
  PW_n = 0;
/*printf ( "highest pattern : %ld (%x)\n" , PW_m , PW_m );*/
  for ( PW_k=0 ; PW_k<(PW_m/2) ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_l*8+32+PW_j*2+PW_k*2]*256)+in_data[PW_Start_Address+PW_l*8+33+PW_j*2+PW_k*2];
/*printf ( "%4x, " , PW_o );*/
    if ( PW_o > PW_n )
      PW_n = PW_o;
  }
/*printf ( "\nhighest : %ld (%x)\n" , PW_n , PW_n );*/
/*printf ( "track data address : %ld (%x)\n" , (34+8*PW_l+2*PW_j+PW_m ),(34+8*PW_l+2*PW_j+PW_m));*/
  PW_n += (34+8*PW_l+2*PW_j+PW_m);
/*printf ( "address of last track : %ld\n" , PW_n );*/
  OutputSize = PW_n;
  

  /* all vars are availlable now, save PW_WholeSampleSize */

  /* now counting size of the last pattern ... pfiew .. */
  PW_l = 0;
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
/*printf ( "%ld," , PW_l );*/
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0xC0 ) == 0xC0 )
    {
      PW_j += (0x100-in_data[PW_Start_Address+PW_n+PW_l]);
      PW_j -= 1;
      PW_l += 1;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0xC0 ) == 0x80 )
    {
      PW_l += 2;
      continue;
    }
    PW_l += 1;
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0x0F ) == 0x00 )
    {
      PW_l += 1;
      continue;
    }
    PW_l += 2;
  }
/*printf ( "\nsize of the last track : %ld\n" , PW_l );*/

  OutputSize += PW_WholeSampleSize + PW_l;

  CONVERT = GOOD;
  Save_Rip ( "Tracker Packer v2 module", TP2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1); /* 0 could be enough */
}



/*
 *   TrackerPacker_v2.c   1998 (c) Asle / ReDoX
 *
 * Converts TP2 packed MODs back to PTK MODs
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 *
 * 28 Nov 1999 : Update
 *   - Speed and Size Optmizings.
*/

void Depack_TP2 ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar poss[37][2];
  Uchar Pats_Numbers[128];
  Uchar *Whatever;
  Uchar Note,Smp,Fx,FxVal;
  Uchar PatMax=0x00;
  Uchar PatPos;
  long Track_Address[128][4];
  long i=0,j=0,k;
  long Start_Pat_Address=999999l;
  long Whole_Sample_Size=0;
  long Max_Track_Address=0;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;/*,*info;*/

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Address , 128*4*4 );
  BZERO ( Pats_Numbers , 128 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b");*/

  /* title */
  Where += 8;
  fwrite ( &in_data[Where] , 20 , 1 , out );
  Where += 20;

  /* number of sample */
  j = ((in_data[Where]*256)+in_data[Where+1])/8;
  Where += 2;
  /*printf ( "number of sample : %ld\n" , j );*/

  Whatever = (Uchar *) malloc (1024);
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
  PatPos = in_data[Where+1];
  Where += 2;
  fwrite ( &PatPos , 1 , 1 , out );

  /* ntk byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  for ( i=0 ; i<PatPos ; i++ )
  {
    Pats_Numbers[i] = ((in_data[Where]*256)+in_data[Where+1])/8;
    Where += 2;
    if ( Pats_Numbers[i] > PatMax )
      PatMax = Pats_Numbers[i];
    /*fprintf ( info , "%3ld: %d\n" , i,Pats_Numbers[i] );*/
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
  }
  /*fprintf ( info , "  (%x)\n" , Max_Track_Address );fflush(info);*/
  /*printf ( "Highest pattern number : %d\n" , PatMax );*/

  /* write pattern list */
  fwrite ( Pats_Numbers , 128 , 1 , out );


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
          Fx    = (c1>>2)&0x0f;
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
        Smp   = ((c2>>4)&0x0f) | ((c1<<4)&0x10);
        Note  = c1&0xFE;
        Fx    = c2&0x0F;
        if ( Fx == 0x00 )
        {
/*fprintf ( info , " <--- No FX !!\n" );*/
          Whatever[k*16+j*4] = Smp&0xf0;
          Whatever[k*16+j*4]   |= poss[(Note/2)][0];
          Whatever[k*16+j*4+1]  = poss[(Note/2)][1];
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
        Whatever[k*16+j*4]   |= poss[(Note/2)][0];
        Whatever[k*16+j*4+1]  = poss[(Note/2)][1];
        Whatever[k*16+j*4+2]  = (Smp<<4)&0xf0;
        Whatever[k*16+j*4+2] |= Fx;
        Whatever[k*16+j*4+3]  = FxVal;
      }
      if ( Where > Max_Track_Address )
        Max_Track_Address = Where;
      /*printf ( "%6ld, " , Max_Track_Address );*/
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "." );*/
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/

  /*printf ( "sample data address : %ld\n" , Max_Track_Address );*/

  /* Sample data */
  fwrite ( &in_data[Max_Track_Address] , Whole_Sample_Size , 1 , out );


  Crap ( " Tracker Packer 2 " , BAD , BAD , out );


  fflush ( out );
  fclose ( out );
  /*fclose ( info );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
