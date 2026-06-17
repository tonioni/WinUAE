/* testPERFSONG */
/* Rip_PERFSONG */
/* Depack_PERFSONG */

#include "globals.h"
#include "extern.h"


int16_t	 testPERFSONG()
{
  /* test 1 */
  PW_Start_Address = PW_i;
  if ((PW_in_size - PW_Start_Address) < 12)
  {
    return BAD;
  }
  
  /* whole file size */
  PW_j = (in_data[PW_Start_Address+8]*256*256*256)
        +(in_data[PW_Start_Address+9]*256*256)
        +(in_data[PW_Start_Address+10]*256)
        +in_data[PW_Start_Address+11] + 12;
  
  /*test 2*/
  /* too big a file ? or too small ? */
  if (((PW_Start_Address + PW_j) > PW_in_size) || (PW_j<802))
  {
    return BAD;
  }
  /* PW_j is the whole packed file size */



  return GOOD;
}




void Rip_PERFSONG ( void )
{
  /* PW_j is the whole file size */

  OutputSize = PW_j;

  CONVERT = GOOD;
  Save_Rip ( "Perfect Song module", PerfSong );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}



/*
 *   PERFSONG.c   20100120 (c) Asle / ReDoX
 *
 * Perfect Song Player to Protracker.
 * format/replayer by Seg/Darkness (~1997)
 *
*/

void Depack_PERFSONG ( void )
{
  uint8_t c1=0x00,c2=0x00;
  uint8_t poss[37][2];
  uint8_t Max=0x00;
  uint8_t Note,Smp,Fx,FxVal;
  uint8_t *Whatever;
  int32_t i=0,j=0,k=0,l,z;
  int32_t Where=PW_Start_Address;
  int32_t SmpAddresses[31];
  int32_t SmpSizes[31];
  int32_t BNRFullSize=0;
  FILE *out;

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

  Whatever = (uint8_t *) malloc (1085);
  BZERO ( Whatever , 1085 );

  /* retrieve full size to fetch sample text at the end */
  BNRFullSize = (in_data[Where+8]*256*256*256)+
      (in_data[Where+1+8]*256*256)+
      (in_data[Where+2+8]*256)+
       in_data[Where+3+8] + 12;
  BNRFullSize -= 702; /* to reach the beginning of the text area */
  printf ( "BNRFullSize at : %d\n",BNRFullSize );

  /* get title */
  for (l=0; l<20 ; l++)
    Whatever[l] = in_data[Where+BNRFullSize+l];
  BNRFullSize += 0x14;

  /* 31 samples */
  Where += 12; /* points on first psmp desc */
  for ( i=0 ; i<31 ; i++ )
  {
    /* read sample address */
    SmpAddresses[i]=(in_data[Where]*256*256*256)+
      (in_data[Where+1]*256*256)+
      (in_data[Where+2]*256)+
       in_data[Where+3];

    /* read loop start address */
    j=(in_data[Where+4]*256*256*256)+
      (in_data[Where+5]*256*256)+
      (in_data[Where+6]*256)+
       in_data[Where+7];
    /* get smptext */
    for (l=0; l<21 ; l++)
      Whatever[i*30+20+l] = in_data[PW_Start_Address+BNRFullSize+l];
    BNRFullSize += 0x16;
    /* read & write sample size */
    Whatever[i*30+42] = in_data[Where+8];
    Whatever[i*30+43] = in_data[Where+9];
    SmpSizes[i] = (((Whatever[i*30+42]*256)+Whatever[i*30+43])*2);
    Whatever[i*30+44] = in_data[Where+12]; /* fine ? */
    Whatever[i*30+45] = in_data[Where+13]; /* vol */
    Whatever[i*30+48] = in_data[Where+10]; /*replen*/
    Whatever[i*30+49] = in_data[Where+11];

    /* calculate loop start value */
    k = (j - SmpAddresses[i])/2;

    /* write loop start */
    /* use of htonl() suggested by Xigh !.*/
    z = htonl(k);
    Whatever[i*30+46] = *((uint8_t *)&z+2);
    Whatever[i*30+47] = *((uint8_t *)&z+3);

    Where += 16;
  }

  /* patternlist size */
  Whatever[950] = in_data[Where+1]+1;
  Whatever[951] = 0x7f;
  Where += 2;

  /* pattern table */
  Max = 0x00;
  for ( c1=0 ; c1<128 ; c1++ )
  {
    j=(in_data[Where]*256*256*256)+
      (in_data[Where+1]*256*256)+
      (in_data[Where+2]*256)+
       in_data[Where+3];
    Whatever[952+c1] = (j-0x3fe)/1024;
    if ( Whatever[952+c1] > Max )
      Max = Whatever[952+c1];
    Where += 4;
  }
  Max += 1;
  /*printf ( "number of pattern : %d\n" , Max );*/

  /* write Protracker's ID */
  Whatever[1080] = 'M';
  Whatever[1081] = '.';
  Whatever[1082] = 'K';
  Whatever[1083] = '.';

  fwrite ( Whatever , 1084 , 1 , out );

  /* pattern data */
  /* Where is already pointing on the 1st note of the 1st pattern */
  for ( i=0 ; i<Max ; i++ )
  {
    BZERO ( Whatever , 1085 );
    for ( j=0 ; j<256 ; j++ )
    {
      Smp  = in_data[Where+j*4+1];
      Note = in_data[Where+j*4];
      Fx   = in_data[Where+j*4+2];
      FxVal = in_data[Where+j*4+3];
      /*printf ("[%lx] %x-%x-%x-%x\n",j*4+Where
      ,in_data[Where+j*4]
      ,in_data[Where+j*4+1]
      ,in_data[Where+j*4+2]
      ,in_data[Where+j*4+3]);*/
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

        case 0x20:  /* tone portamento + vol slide DOWN */
          Fx = 0x05;
          break;

        case 0x24:  /* vibrato + volume slide UP */
          Fx = 0x06;
          c1 = (FxVal << 4)&0xf0;
          c2 = (FxVal >> 4)&0x0f;
          FxVal = c1|c2;
          break;

        case 0x28:  /* vibrato + volume slide DOWN */
          Fx = 0x06;
          break;

        case 0x2c:  /* vibrato + volume slide DOWN */
          Fx = 0x06;
          break;

        case 0x30:  /* tremolo */
          Fx = 0x07;
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

        case 0x74:  /* fine volume slide up */
          Fx = 0x0E;
          FxVal |= 0xa0;
          break;

        case 0x78:  /* fine volume slide down */
          Fx = 0x0E;
          FxVal |= 0xb0;
          break;

        case 0x7c:  /* pattern delay */
          Fx = 0x0E;
          FxVal |= 0xe0;
          break;

        default:
          printf ( "%x : at %d (out:%ld)\n" , Fx , Where+(j*4),ftell(out) );
          Fx = 0x00;
          break;
      }
      Note /= 2;
      c1 = (Smp>>4) & 0x0f;
      c2 = (Smp<<4) & 0xf0;
      Smp = c1 | c2;
      Whatever[j*4] = (Smp & 0xf0);
      Whatever[j*4] |= poss[(Note)][0];
      Whatever[j*4+1] = poss[(Note)][1];
      Whatever[j*4+2] = ((Smp<<4)&0xf0);
      Whatever[j*4+2] |= Fx;
      Whatever[j*4+3] = FxVal;
    }
    Where += 1024;
    fwrite ( Whatever , 1024 , 1 , out );
    printf ( "pattern %d written (Where : %d)\n" , i ,Where);
  }
  free ( Whatever );

  /* sample data */
  for (i=0; i<31; i++)
  {
    if ( SmpSizes[i] == 0 )
      continue;
    fwrite ( &in_data[PW_Start_Address + SmpAddresses[i]] , SmpSizes[i] , 1 , out );
  }

  /* no crap as it destroys sample text */
  /*Crap ( " PERFSONG Packer  " , BAD , BAD , out );*/

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
