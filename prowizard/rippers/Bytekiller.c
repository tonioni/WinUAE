/* testByteKiller_13()   */
/* testByteKiller_20()   */
/* testByteKiller30()    */
/* testbytekillerpro10() */
/* Rip_ByteKiller()      */
/* Rip_ByteKiller30()    */
/* Rip_bytekillerpro10   */

#include "globals.h"
#include "extern.h"

short testByteKiller_13 ( void )
{
  /*  if ( PW_i < 135 )*/
  if ( test_1_start (135) == BAD )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }

  PW_Start_Address = PW_i-135;

  if ( (in_data[PW_Start_Address]    != 0x41 ) ||
       (in_data[PW_Start_Address+1]  != 0xFA ) ||
       (in_data[PW_Start_Address+2]  != 0x00 ) ||
       (in_data[PW_Start_Address+3]  != 0xE6 ) ||
       (in_data[PW_Start_Address+4]  != 0x43 ) ||
       (in_data[PW_Start_Address+5]  != 0xF9 ) ||
       (in_data[PW_Start_Address+10] != 0x20 ) ||
       (in_data[PW_Start_Address+11] != 0x18 ) ||
       (in_data[PW_Start_Address+12] != 0x22 ) ||
       (in_data[PW_Start_Address+13] != 0x18 ) ||
       (in_data[PW_Start_Address+14] != 0x2A ) ||
       (in_data[PW_Start_Address+15] != 0x18 ) ||
       (in_data[PW_Start_Address+16] != 0x24 ) ||
       (in_data[PW_Start_Address+17] != 0x49 ) ||
       (in_data[PW_Start_Address+18] != 0xD1 ) ||
       (in_data[PW_Start_Address+19] != 0xC0 ) ||
       (in_data[PW_Start_Address+20] != 0xD5 ) ||
       (in_data[PW_Start_Address+21] != 0xC1 ) ||
       (in_data[PW_Start_Address+22] != 0x20 ) ||
       (in_data[PW_Start_Address+23] != 0x20 ) ||
       (in_data[PW_Start_Address+24] != 0xB1 ) ||
       (in_data[PW_Start_Address+25] != 0x85 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+232]*256*256*256) +
           (in_data[PW_Start_Address+233]*256*256) +
           (in_data[PW_Start_Address+234]*256) +
           in_data[PW_Start_Address+235] );

  PW_l += 304;

  if ( PW_i >= 171 )
  {
    if ( (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) ||
         (in_data[PW_Start_Address-34]  != 0x03 ) ||
         (in_data[PW_Start_Address-33]  != 0xF3 ) ||
         (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x00 ) ||
         (in_data[PW_Start_Address-29]  != 0x00 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x02 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;

  return GOOD;
  /* PW_l is the size of the pack */
}



short testByteKiller_20 ( void )
{
  if ( test_1_start(127) == BAD )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-127;

  if ( (in_data[PW_Start_Address]    != 0x48 ) ||
       (in_data[PW_Start_Address+1]  != 0xE7 ) ||
       (in_data[PW_Start_Address+2]  != 0xFF ) ||
       (in_data[PW_Start_Address+3]  != 0xFE ) ||
       (in_data[PW_Start_Address+4]  != 0x4D ) ||
       (in_data[PW_Start_Address+5]  != 0xF9 ) ||
       (in_data[PW_Start_Address+6]  != 0x00 ) ||
       (in_data[PW_Start_Address+7]  != 0xDF ) ||
       (in_data[PW_Start_Address+8]  != 0xF1 ) ||
       (in_data[PW_Start_Address+10] != 0x41 ) ||
       (in_data[PW_Start_Address+11] != 0xFA ) ||
       (in_data[PW_Start_Address+12] != 0x00 ) ||
       (in_data[PW_Start_Address+13] != 0xBA ) ||
       (in_data[PW_Start_Address+14] != 0x43 ) ||
       (in_data[PW_Start_Address+15] != 0xF9 ) ||
       (in_data[PW_Start_Address+20] != 0x20 ) ||
       (in_data[PW_Start_Address+21] != 0x18 ) ||
       (in_data[PW_Start_Address+22] != 0x22 ) ||
       (in_data[PW_Start_Address+23] != 0x18 ) ||
       (in_data[PW_Start_Address+24] != 0x2A ) ||
       (in_data[PW_Start_Address+25] != 0x18 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+198]*256*256*256) +
           (in_data[PW_Start_Address+199]*256*256) +
           (in_data[PW_Start_Address+200]*256) +
           in_data[PW_Start_Address+201] );

  PW_l += 272;

  if ( PW_i >= 163 )
  {
    if ( (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) ||
         (in_data[PW_Start_Address-34]  != 0x03 ) ||
         (in_data[PW_Start_Address-33]  != 0xF3 ) ||
         (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x00 ) ||
         (in_data[PW_Start_Address-29]  != 0x00 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x02 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;
 
  return GOOD;
  /* PW_l is the size of the pack */
}


short testByteKiller30 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x00 ) ||
       (in_data[PW_Start_Address+17] != 0xDF ) ||
       (in_data[PW_Start_Address+18] != 0xF1 ) ||
       (in_data[PW_Start_Address+19] != 0x80 ) ||
       (in_data[PW_Start_Address+20] != 0x20 ) ||
       (in_data[PW_Start_Address+21] != 0x18 ) ||
       (in_data[PW_Start_Address+22] != 0x22 ) ||
       (in_data[PW_Start_Address+23] != 0x18 ) ||
       (in_data[PW_Start_Address+24] != 0xD1 ) ||
       (in_data[PW_Start_Address+25] != 0xC0 ) ||
       (in_data[PW_Start_Address+26] != 0x20 ) ||
       (in_data[PW_Start_Address+27] != 0x10 ) ||
       (in_data[PW_Start_Address+28] != 0x24 ) ||
       (in_data[PW_Start_Address+29] != 0x49 ) ||
       (in_data[PW_Start_Address+30] != 0xD5 ) ||
       (in_data[PW_Start_Address+31] != 0xC1 ) ||
       (in_data[PW_Start_Address+32] != 0x7A ) ||
       (in_data[PW_Start_Address+33] != 0x03 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+188]*256*256*256) +
           (in_data[PW_Start_Address+189]*256*256) +
           (in_data[PW_Start_Address+190]*256) +
           in_data[PW_Start_Address+191] );

  PW_l += 236;


  if ( PW_i >= 32 )
  {
    if ( (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x03 ) ||
         (in_data[PW_Start_Address-29]  != 0xF3 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x00 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x01 ) ||
         (in_data[PW_Start_Address-20]  != 0x00 ) ||
         (in_data[PW_Start_Address-19]  != 0x00 ) ||
         (in_data[PW_Start_Address-18]  != 0x00 ) ||
         (in_data[PW_Start_Address-17]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;

  return GOOD;
  /* PW_l is the size of the pack */
}

short testbytekillerpro10 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+22] != 0x00 ) ||
       (in_data[PW_Start_Address+23] != 0x04 ) ||
       (in_data[PW_Start_Address+24] != 0x2A ) ||
       (in_data[PW_Start_Address+25] != 0x28 ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x08 ) ||
       (in_data[PW_Start_Address+28] != 0x41 ) ||
       (in_data[PW_Start_Address+29] != 0xE8 ) ||
       (in_data[PW_Start_Address+30] != 0x00 ) ||
       (in_data[PW_Start_Address+31] != 0x0C ) ||
       (in_data[PW_Start_Address+32] != 0x24 ) ||
       (in_data[PW_Start_Address+33] != 0x49 ) ||
       (in_data[PW_Start_Address+34] != 0xD1 ) ||
       (in_data[PW_Start_Address+35] != 0xC0 ) ||
       (in_data[PW_Start_Address+36] != 0xD5 ) ||
       (in_data[PW_Start_Address+37] != 0xC1 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+222]*256*256*256) +
           (in_data[PW_Start_Address+223]*256*256) +
           (in_data[PW_Start_Address+224]*256) +
           in_data[PW_Start_Address+225] );

  PW_l += 308;


  if ( PW_i >= 50 )
  {
    if ( (in_data[PW_Start_Address-50]  != 0x00 ) ||
         (in_data[PW_Start_Address-49]  != 0x00 ) ||
         (in_data[PW_Start_Address-48]  != 0x03 ) ||
         (in_data[PW_Start_Address-47]  != 0xF3 ) ||
         (in_data[PW_Start_Address-46]  != 0x00 ) ||
         (in_data[PW_Start_Address-45]  != 0x00 ) ||
         (in_data[PW_Start_Address-44]  != 0x00 ) ||
         (in_data[PW_Start_Address-43]  != 0x00 ) ||
         (in_data[PW_Start_Address-42]  != 0x00 ) ||
         (in_data[PW_Start_Address-41]  != 0x00 ) ||
         (in_data[PW_Start_Address-40]  != 0x00 ) ||
         (in_data[PW_Start_Address-39]  != 0x02 ) ||
         (in_data[PW_Start_Address-38]  != 0x00 ) ||
         (in_data[PW_Start_Address-37]  != 0x00 ) ||
         (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;

  return GOOD;
  /* PW_l is the size of the pack */
}


void Rip_ByteKiller ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 36;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 36 );
    BZERO ( Amiga_EXE_Header_Block , 36 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[30] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x02;
    Amiga_EXE_Header_Block[19] = 0x01;
    Amiga_EXE_Header_Block[27] = 0x01;
    Amiga_EXE_Header_Block[31] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 60;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = *Whatever;

    /* also the last 24 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-24] = 0x00;
    in_data[PW_Start_Address+OutputSize-23] = 0x00;
    in_data[PW_Start_Address+OutputSize-22] = 0x03;
    in_data[PW_Start_Address+OutputSize-21] = 0xEC;

    in_data[PW_Start_Address+OutputSize-20] = 0x00;
    in_data[PW_Start_Address+OutputSize-19] = 0x00;
    in_data[PW_Start_Address+OutputSize-18] = 0x00;
    in_data[PW_Start_Address+OutputSize-17] = 0x00;

    in_data[PW_Start_Address+OutputSize-16] = 0x00;
    in_data[PW_Start_Address+OutputSize-15] = 0x00;
    in_data[PW_Start_Address+OutputSize-14] = 0x03;
    in_data[PW_Start_Address+OutputSize-13] = 0xF2;

    in_data[PW_Start_Address+OutputSize-12] = 0x00;
    in_data[PW_Start_Address+OutputSize-11] = 0x00;
    in_data[PW_Start_Address+OutputSize-10] = 0x03;
    in_data[PW_Start_Address+OutputSize-9]  = 0xEB;

    in_data[PW_Start_Address+OutputSize-8]  = 0x00;
    in_data[PW_Start_Address+OutputSize-7]  = 0x00;
    in_data[PW_Start_Address+OutputSize-6]  = 0x00;
    in_data[PW_Start_Address+OutputSize-5]  = 0x01;

    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "ByteKiller 1.3/2.0 Exe-file", ByteKiller, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "ByteKiller 1.3/2.0 Exe-file", ByteKiller );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 173;  /* 171 should do but call it "just to be sure" :) */
}

void Rip_ByteKiller30 ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 32;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 32 );
    BZERO ( Amiga_EXE_Header_Block , 32 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[26] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x01;
    Amiga_EXE_Header_Block[27] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 36;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[28] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[29] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[30] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[31] = *Whatever;
    Save_Rip_Special ( "ByteKiller 3.0 Exe-file", ByteKiller,  Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 32;
    Save_Rip ( "ByteKiller 3.0 Exe-file", ByteKiller );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 36;  /* 32 should do but call it "just to be sure" :) */
}


void Rip_bytekillerpro10 ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 50;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 36 );
    BZERO ( Amiga_EXE_Header_Block , 36 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[30] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x02;
    Amiga_EXE_Header_Block[19] = 0x01;
    Amiga_EXE_Header_Block[27] = 0x01;
    Amiga_EXE_Header_Block[31] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 60;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = *Whatever;

    /* also the last 24 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-24] = 0x00;
    in_data[PW_Start_Address+OutputSize-23] = 0x00;
    in_data[PW_Start_Address+OutputSize-22] = 0x03;
    in_data[PW_Start_Address+OutputSize-21] = 0xEC;

    in_data[PW_Start_Address+OutputSize-20] = 0x00;
    in_data[PW_Start_Address+OutputSize-19] = 0x00;
    in_data[PW_Start_Address+OutputSize-18] = 0x00;
    in_data[PW_Start_Address+OutputSize-17] = 0x00;

    in_data[PW_Start_Address+OutputSize-16] = 0x00;
    in_data[PW_Start_Address+OutputSize-15] = 0x00;
    in_data[PW_Start_Address+OutputSize-14] = 0x03;
    in_data[PW_Start_Address+OutputSize-13] = 0xF2;

    in_data[PW_Start_Address+OutputSize-12] = 0x00;
    in_data[PW_Start_Address+OutputSize-11] = 0x00;
    in_data[PW_Start_Address+OutputSize-10] = 0x03;
    in_data[PW_Start_Address+OutputSize-9]  = 0xEB;

    in_data[PW_Start_Address+OutputSize-8]  = 0x00;
    in_data[PW_Start_Address+OutputSize-7]  = 0x00;
    in_data[PW_Start_Address+OutputSize-6]  = 0x00;
    in_data[PW_Start_Address+OutputSize-5]  = 0x01;

    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "ByteKillerPro 1.0 Exe-file", ByteKiller, Amiga_EXE_Header_Block , 50 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 50;
    Save_Rip ( "ByteKillerPro 1.0 Exe-file", ByteKiller );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 54;  /* 51 should do but call it "just to be sure" :) */
}

