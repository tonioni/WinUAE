/* testZEN() */
/* Rip_ZEN() */
/* Depack_ZEN() */

#include "globals.h"
#include "extern.h"


short testZEN ( void )
{
  /* test #1 */
  if ( PW_i<9 )
  {
    return BAD;
  }

  /* test #2 */
  PW_Start_Address = PW_i-9;
  PW_l = ( (in_data[PW_Start_Address]*256*256*256)+
	   (in_data[PW_Start_Address+1]*256*256)+
	   (in_data[PW_Start_Address+2]*256)+
	   in_data[PW_Start_Address+3] );
  if ( (PW_l<502) || (PW_l>2163190l) || (PW_l>(PW_in_size-PW_Start_Address)) )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_l is the address of the pattern list */

  /* volumes */
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    if ( (( PW_Start_Address + 9 + (16*PW_k) ) > PW_in_size )||
	 (( in_data[PW_Start_Address + 9 + (16*PW_k)] > 0x40 )))
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* fine */
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    PW_j = (in_data[PW_Start_Address+6+(PW_k*16)]*256)+in_data[PW_Start_Address+7+(PW_k*16)];
    if ( ((PW_j/72)*72) != PW_j )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* smp sizes .. */
  PW_n = 0;
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    PW_o = (in_data[PW_Start_Address+10+PW_k*16]*256)+in_data[PW_Start_Address+11+PW_k*16];
    PW_m = (in_data[PW_Start_Address+12+PW_k*16]*256)+in_data[PW_Start_Address+13+PW_k*16];
    PW_j = ((in_data[PW_Start_Address+14+PW_k*16]*256*256*256)+
	    (in_data[PW_Start_Address+15+PW_k*16]*256*256)+
	    (in_data[PW_Start_Address+16+PW_k*16]*256)+
	    in_data[PW_Start_Address+17+PW_k*16] );
    PW_o *= 2;
    PW_m *= 2;
    /* sample size and loop size > 64k ? */
    if ( (PW_o > 0xFFFF)||
	 (PW_m > 0xFFFF) )
    {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* sample address < pattern table address ? */
    if ( PW_j < PW_l )
    {
/*printf ( "#4,1 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* too big an address ? */
    if ( PW_j > PW_in_size )
    {
/*printf ( "#4,2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
    /* get the nbr of the highest sample address and its size */
    if ( PW_j > PW_n )
    {
      PW_n = PW_j;
      PW_WholeSampleSize = PW_o;
    }
  }
  /* PW_n is the highest sample data address */
  /* PW_WholeSampleSize is the size of the same sample */

  /* test size of the pattern list */
  PW_j = in_data[PW_Start_Address+5];
  if ( (PW_j > 0x7f) || (PW_j == 0) )
  {
/*printf ( "#5 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }


  /* test if the end of pattern list is $FFFFFFFF */
  if ( (in_data[PW_Start_Address+PW_l+PW_j*4] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+1] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+2] != 0xFF ) ||
       (in_data[PW_Start_Address+PW_l+PW_j*4+3] != 0xFF ) )
  {
/*printf ( "#6 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_n is the highest address of a sample data */
  /* PW_WholeSampleSize is its size */
  return GOOD;
}



void Rip_ZEN ( void )
{
  /* PW_n is the highest sample data address */
  /* PW_WholeSampleSize if its size */

  OutputSize = PW_WholeSampleSize + PW_n;

  CONVERT = GOOD;
  Save_Rip ( "ZEN Packer module", ZEN );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 10);  /* 9 should do but call it "just to be sure" :) */
}



/*
 *   Zen_Packer.c   1998 (c) Asle / ReDoX
 *
 * Converts ZEN packed MODs back to PTK MODs
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 Nov 1999 : Update
 *   - Speed an size optimizings.
 * 03 april 2000 : Update
 *   - small bug correction (harmless)
 *     again pointed out by Thomas Neumann
 * Another Update : 26 nov 2003
 *   - used htonl() so that use of addy is now portable on 68k archs
*/

void Depack_ZEN ( void )
{
  Uchar PatPos;
  Uchar *Whatever;
  Uchar PatMax;
  Uchar poss[37][2];
  Uchar Note,Smp,Fx,FxVal;
  long WholeSampleSize=0;
  long Pattern_Address[128];
  long Pattern_Address_Real[128];
  long Pattern_Table_Address;
  long Sample_Data_Address=999999l;
  long i,j,k,z;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  BZERO ( Pattern_Address , 128*4);
  BZERO ( Pattern_Address_Real , 128*4);
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* read pattern table address */
  Pattern_Table_Address = (in_data[Where]*256*256*256)+
                          (in_data[Where+1]*256*256)+
                          (in_data[Where+2]*256)+
                           in_data[Where+3];
  Where += 4;

  /* read patmax */
  PatMax = in_data[Where++];

  /* read size of pattern table */
  PatPos = in_data[Where++];
  /*printf ( "Size of pattern list : %d\n" , PatPos );*/

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( Whatever , 22 , 1 , out );

    /* read sample size */
    WholeSampleSize += (((in_data[Where+4]*256)+in_data[Where+5])*2);
    fwrite ( &in_data[Where+4] , 2 , 1 , out );

    /* write fine */
    Whatever[32] = ((in_data[Where]*256)+in_data[Where+1])/0x48;
    fwrite ( &Whatever[32] , 1 , 1 , out );

    /* write volume */
    fwrite ( &in_data[Where+3] , 1 , 1 , out );

    /* read sample start address */
    k = (in_data[Where+8]*256*256*256)+
        (in_data[Where+9]*256*256)+
        (in_data[Where+10]*256)+
         in_data[Where+11];

    if ( k<Sample_Data_Address )
      Sample_Data_Address = k;

    /* read loop start address */
    j = (in_data[Where+12]*256*256*256)+
        (in_data[Where+13]*256*256)+
        (in_data[Where+14]*256)+
         in_data[Where+15];
    j -= k;
    j /= 2;

    /* write loop start */
    /* use of htonl() suggested by Xigh !.*/
    z = htonl(j);
    Whatever[48] = *((Uchar *)&z+2);
    Whatever[49] = *((Uchar *)&z+3);
    fwrite ( &Whatever[48] , 2 , 1 , out );

    /* write loop size */
    fwrite ( &in_data[Where+6] , 2 , 1 , out );

    Where += 16;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* write size of pattern list */
  fwrite ( &PatPos , 1 , 1 , out );

  /* write ntk byte */
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );

  /* read pattern table .. */
  Where = PW_Start_Address + Pattern_Table_Address;
  for ( i=0 ; i<PatPos ; i++ )
  {
    Pattern_Address[i] = (in_data[Where]*256*256*256)+
                         (in_data[Where+1]*256*256)+
                         (in_data[Where+2]*256)+
                          in_data[Where+3];
    Where += 4;
  }

  /* deduce pattern list */
  Whatever[256] = 0x00;
  for ( i=0 ; i<PatPos ; i++ )
  {
    if ( i == 0 )
    {
      Whatever[0] = 0x00;
      Pattern_Address_Real[0] = Pattern_Address[0];
      Whatever[256] += 0x01;
      continue;
    }
    for ( j=0 ; j<i ; j++ )
    {
      if ( Pattern_Address[i] == Pattern_Address[j] )
      {
        Whatever[i] = Whatever[j];
        break;
      }
    }
    if ( j == i )
    {
      Pattern_Address_Real[Whatever[256]] = Pattern_Address[i];
      Whatever[i] = Whatever[256];
      Whatever[256] += 0x01;
    }
  }
  /*printf ( "Number of pattern : %d\n" , PatMax );*/

  /* write pattern table */
  fwrite ( Whatever , 128 , 1 , out );

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  /*printf ( "converting pattern datas " );*/
  for ( i=0 ; i<=PatMax ; i++ )
  {
    BZERO ( Whatever , 1024 );
    Where = PW_Start_Address + Pattern_Address_Real[i];
/*fprintf ( info , "\n\nPattern %ld:\n" , i );*/
    for ( j=0 ; j<256 ; j++ )
    {

      Note = (in_data[Where+1]&0x7f)/2;
      FxVal = in_data[Where+3];
      Smp = ((in_data[Where+1]<<4)&0x10)|((in_data[Where+2]>>4)&0x0f);
      Fx = in_data[Where+2]&0x0f;

/*fprintf ( info , "<-- note:%-2x  smp:%-2x  fx:%-2x  fxval:%-2x\n"
               , Note,Smp,Fx,FxVal );*/

      k=in_data[Where];
      Whatever[k*4]   = Smp&0xf0;
      Whatever[k*4]  |= poss[Note][0];
      Whatever[k*4+1] = poss[Note][1];
      Whatever[k*4+2] = Fx | ((Smp<<4)&0xf0);
      Whatever[k*4+3] = FxVal;
      j = in_data[Where];
      Where += 4;
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "." );*/
  }
  free ( Whatever );
  /*printf ( " ok\n" );*/


  /* sample data */
  fwrite ( &in_data[PW_Start_Address+Sample_Data_Address]  , WholeSampleSize , 1 , out );

  /* crap ... */
  Crap ( "    ZEN Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
