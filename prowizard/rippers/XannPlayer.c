/* testXANN() */
/* Rip_XANN() */
/* Depack_XANN() */

#include "globals.h"
#include "extern.h"


short testXANN ( void )
{
  /* test 1 */
  if ( PW_i < 3 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i - 3;
  for ( PW_l=0 ; PW_l<128 ; PW_l++ )
  {
    PW_j = (in_data[PW_Start_Address+PW_l*4]*256*256*256)
          +(in_data[PW_Start_Address+PW_l*4+1]*256*256)
          +(in_data[PW_Start_Address+PW_l*4+2]*256)
          +in_data[PW_Start_Address+PW_l*4+3];
    PW_k = (PW_j/4)*4;
    if ( (PW_k != PW_j) || (PW_j>132156) )
    {
/*printf ( "#2 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test 3 */
  if ( (PW_in_size - PW_Start_Address) < 2108)
  {
/*printf ( "#3 (Start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test 4 */
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
    if ( (in_data[PW_Start_Address+3+PW_j*4] != 0x3c) &&
	 (in_data[PW_Start_Address+3+PW_j*4] != 0x00) )
    {
/*printf ( "#4 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /* test 5 */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+519+16*PW_j] > 0x40 )
    {
/*printf ( "#5 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+530+PW_j*16]*256)+in_data[PW_Start_Address+531+PW_j*16])*2);
  }

  /* test #6  (address of samples) */
  for ( PW_l=0 ; PW_l<30 ; PW_l++ )
  {
    PW_k = (in_data[PW_Start_Address+526+16*PW_l]*256*256*256)
      +(in_data[PW_Start_Address+527+16*PW_l]*256*256)
      +(in_data[PW_Start_Address+528+16*PW_l]*256)
      +in_data[PW_Start_Address+529+16*PW_l];
    PW_j = (((in_data[PW_Start_Address+524+16*PW_l]*256)
      +in_data[PW_Start_Address+525+16*PW_l])*2);
    PW_m = (in_data[PW_Start_Address+520+16*(PW_l+1)]*256*256*256)
      +(in_data[PW_Start_Address+521+16*(PW_l+1)]*256*256)
      +(in_data[PW_Start_Address+522+16*(PW_l+1)]*256)
      +in_data[PW_Start_Address+523+16*(PW_l+1)];
    if ( (PW_k<2108) || (PW_m<2108) )
    {
/*printf ( "#6 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
    if ( PW_k > PW_m )
    {
/*printf ( "#6,1 (Start:%ld) (PW_k:%ld) (PW_j:%ld) (PW_m:%ld)\n" , PW_Start_Address,PW_k,PW_j,PW_m );*/
      return BAD;
    }
  }

  /* test #7  first pattern data .. */
  for ( PW_j=0 ; PW_j<256 ; PW_j++ )
  {
    PW_k = (in_data[PW_Start_Address+PW_j*4+1085]/2);
    PW_l = PW_k*2;
    if ( in_data[PW_Start_Address+PW_j*4+1085] != PW_l )
    {
/*printf ( "#7 (Start:%ld)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  return GOOD;
}




void Rip_XANN ( void )
{
  /* PW_WholeSampleSize is the whole sample size */

  PW_k=0;
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
    PW_l = in_data[PW_Start_Address+2+PW_j*4];
    if ( PW_l > PW_k )
      PW_k = PW_l;
  }
  PW_k /= 4;
  OutputSize = PW_WholeSampleSize + 1084 + (PW_k*1024);

  CONVERT = GOOD;
  Save_Rip ( "Xann Packer module", XANN_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 4);  /* 3 should do but call it "just to be sure" :) */
}



/*
 *   XANN_Packer.c   1997 (c) Asle / ReDoX
 *
 * XANN Packer to Protracker.
 *
 *
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 Nov 1999 : Update
 *   - Speed & Size optimizings
 * Another Update : 26 nov 2003
 *   - used htonl() so that use of addy is now portable on 68k archs
*/

#define SMP_DESC_ADDRESS 0x206
#define PAT_DATA_ADDRESS 0x43C

void Depack_XANN ( void )
{
  Uchar c1=0x00,c2=0x00;
  Uchar poss[37][2];
  Uchar Max=0x00;
  Uchar Note,Smp,Fx,FxVal;
  Uchar *Whatever;
  long i=0,j=0,l=0,z;
  long WholeSampleSize=0;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  /* title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* 31 samples */
  Where = PW_Start_Address + SMP_DESC_ADDRESS;
  for ( i=0 ; i<31 ; i++ )
  {
    /* sample name */
    fwrite ( &Whatever[2] , 22 , 1 , out );

    /* read loop start address */
    j=(in_data[Where+2]*256*256*256)+
      (in_data[Where+3]*256*256)+
      (in_data[Where+4]*256)+
       in_data[Where+5];

    /* read sample address */
    l=(in_data[Where+8]*256*256*256)+
      (in_data[Where+9]*256*256)+
      (in_data[Where+10]*256)+
       in_data[Where+11];

    /* read & write sample size */
    fwrite ( &in_data[Where+12] , 2 , 1 , out );
    WholeSampleSize += (((in_data[Where+12]*256)+in_data[Where+13])*2);

    /* calculate loop start value */
    j = j-l;

    /* write fine & vol */
    fwrite ( &in_data[Where] , 2 , 1 , out );

    /* write loop start */
    /* use of htonl() suggested by Xigh !.*/
    j/=2;
    z = htonl(j);
    Whatever[0] = *((Uchar *)&z+2);
    Whatever[1] = *((Uchar *)&z+3);
    fwrite ( Whatever , 2 , 1 , out );

    /* write loop size */
    fwrite ( &in_data[Where+6] , 2 , 1 , out );

    Where += 16;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* pattern table */
  Max = 0x00;
  Where = PW_Start_Address;
  for ( c1=0 ; c1<128 ; c1++ )
  {
    l=(in_data[Where]*256*256*256)+
      (in_data[Where+1]*256*256)+
      (in_data[Where+2]*256)+
       in_data[Where+3];
    Where += 4;
    if ( l == 0 )
      break;
    Whatever[c1] = ((l-0x3c)/1024)-1;
    if ( Whatever[c1] > Max )
      Max = Whatever[c1];
  }
  Max += 1;  /* starts at $00 */
  /*printf ( "number of pattern : %d\n" , Max );*/

  /* write number of pattern */
  fwrite ( &c1 , 1 , 1 , out );

  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* write pattern list */
  fwrite ( Whatever , 128 , 1 , out );

  /* write Protracker's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* pattern data */
  Where = PW_Start_Address + PAT_DATA_ADDRESS;
  for ( i=0 ; i<Max ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<256 ; j++ )
    {
      Smp  = (in_data[Where+j*4]>>3)&0x1f;
      Note = in_data[Where+j*4+1];
      Fx   = in_data[Where+j*4+2];
      FxVal = in_data[Where+j*4+3];
      switch ( Fx )
      {
        case 0x00:  /* no Fx */
          Fx = 0x00;
          break;

        case 0x04:  /* arpeggio */
          Fx = 0x00;
          break;

        case 0x08:  /* portamento up */
          Fx = 0x01;
          break;

        case 0x0C:  /* portamento down */
          Fx = 0x02;
          break;

        case 0x10:  /* tone portamento with no FxVal */
          Fx = 0x03;
          break;

        case 0x14:  /* tone portamento */
          Fx = 0x03;
          break;

        case 0x18:  /* vibrato with no FxVal */
          Fx = 0x04;
          break;

        case 0x1C:  /* vibrato */
          Fx = 0x04;
          break;

        case 0x24:  /* tone portamento + vol slide DOWN */
          Fx = 0x05;
          break;

        case 0x28:  /* vibrato + volume slide UP */
          Fx = 0x06;
          c1 = (FxVal << 4)&0xf0;
          c2 = (FxVal >> 4)&0x0f;
          FxVal = c1|c2;
          break;

        case 0x2C:  /* vibrato + volume slide DOWN */
          Fx = 0x06;
          break;

        case 0x38:  /* sample offset */
          Fx = 0x09;
          break;

        case 0x3C: /* volume slide up */
          Fx = 0x0A;
          c1 = (FxVal << 4)&0xf0;
          c2 = (FxVal >> 4)&0x0f;
          FxVal = c1|c2;
          break;

        case 0x40: /* volume slide down */
          Fx = 0x0A;
          break;

        case 0x44: /* position jump */
          Fx = 0x0B;
          break;

        case 0x48: /* set volume */
          Fx = 0x0C;
          break;

        case 0x4C: /* pattern break */
          Fx = 0x0D;
          break;

        case 0x50: /* set speed */
          Fx = 0x0F;
          break;

        case 0x58: /* set filter */
          Fx = 0x0E;
          FxVal = 0x01;
          break;

        case 0x5C:  /* fine slide up */
          Fx = 0x0E;
          FxVal |= 0x10;
          break;

        case 0x60:  /* fine slide down */
          Fx = 0x0E;
          FxVal |= 0x20;
          break;

        case 0x74:  /* set loop start */
        case 0x78:  /* set loop value */
          Fx = 0x0E;
          FxVal |= 0x60;
          break;

        case 0x84:  /* retriger */
          Fx = 0x0E;
          FxVal |= 0x90;
          break;

        case 0x88:  /* fine volume slide up */
          Fx = 0x0E;
          FxVal |= 0xa0;
          break;

        case 0x8C:  /* fine volume slide down */
          Fx = 0x0E;
          FxVal |= 0xb0;
          break;

        case 0x94:  /* note delay */
          Fx = 0x0E;
          FxVal |= 0xd0;
          break;

        case 0x98:  /* pattern delay */
          Fx = 0x0E;
          FxVal |= 0xe0;
          break;

        default:
          /*printf ( "%x : at %ld (out:%ld)\n" , Fx , Where+(j*4),ftell(out) );*/
          Fx = 0x00;
          break;
      }
      Whatever[j*4] = (Smp & 0xf0);
      Whatever[j*4] |= poss[(Note/2)][0];
      Whatever[j*4+1] = poss[(Note/2)][1];
      Whatever[j*4+2] = ((Smp<<4)&0xf0);
      Whatever[j*4+2] |= Fx;
      Whatever[j*4+3] = FxVal;
    }
    Where += 1024;
    fwrite ( Whatever , 1024 , 1 , out );
/*    printf ( "pattern %ld written\n" , i );*/
  }
  free ( Whatever );

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  Crap ( "   XANN Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}

