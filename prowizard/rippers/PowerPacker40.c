/* testPowerpacker40() */
/* Rip_Powerpacker40() */

#include "globals.h"
#include "extern.h"


short testPowerpacker40 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0xD1 ) ||
       (in_data[PW_Start_Address+17] != 0xC8 ) ||
       (in_data[PW_Start_Address+18] != 0x58 ) ||
       (in_data[PW_Start_Address+19] != 0x48 ) ||
       (in_data[PW_Start_Address+20] != 0x26 ) ||
       (in_data[PW_Start_Address+21] != 0x48 ) ||
       (in_data[PW_Start_Address+22] != 0x50 ) ||
       (in_data[PW_Start_Address+23] != 0x4B ) ||
       (in_data[PW_Start_Address+24] != 0x2C ) ||
       (in_data[PW_Start_Address+25] != 0x78 ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x04 ) ||
       (in_data[PW_Start_Address+28] != 0x2F ) ||
       (in_data[PW_Start_Address+29] != 0x08 ) ||
       (in_data[PW_Start_Address+30] != 0xD1 ) ||
       (in_data[PW_Start_Address+31] != 0xFC ) ||
       (in_data[PW_Start_Address+36] != 0x61 ) ||
       (in_data[PW_Start_Address+37] != 0x00 ) ||
       (in_data[PW_Start_Address+40] != 0x20 ) ||
       (in_data[PW_Start_Address+41] != 0x57 ) ||
       (in_data[PW_Start_Address+42] != 0x51 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }
  if ((PW_Start_Address + 684) > PW_in_size)
  {
    return BAD;
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+32]*256*256*256) +
           (in_data[PW_Start_Address+33]*256*256) +
           (in_data[PW_Start_Address+34]*256) +
           in_data[PW_Start_Address+35] );
  PW_l += 684;

  if ((PW_l > PW_in_size) || (PW_l > 2000000l))
  {
    return BAD;
  }

  if ( PW_i >= 36 )
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



void Rip_Powerpacker40 ( void )
{
  /* PW_l is still the whole size */


  Uchar * Amiga_EXE_Header_Block;

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
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = 0x9E;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Save_Rip_Special ( "Powerpacker 4.0 Exe-file", Powerpacker4, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Powerpacker 4.0 Exe-file", Powerpacker4 );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 40);  /* 36 should do but call it "just to be sure" :) */
}

