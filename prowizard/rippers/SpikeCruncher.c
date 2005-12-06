/* testSpikeCruncher() */
/* Rip_SpikeCruncher() */


#include "globals.h"
#include "extern.h"


short testSpikeCruncher ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+38] != 0x2C ) ||
       (in_data[PW_Start_Address+39] != 0x78 ) ||
       (in_data[PW_Start_Address+40] != 0x00 ) ||
       (in_data[PW_Start_Address+41] != 0x04 ) ||
       (in_data[PW_Start_Address+42] != 0x22 ) ||
       (in_data[PW_Start_Address+43] != 0x4B ) ||
       (in_data[PW_Start_Address+44] != 0x24 ) ||
       (in_data[PW_Start_Address+45] != 0x4B ) ||
       (in_data[PW_Start_Address+46] != 0x2F ) ||
       (in_data[PW_Start_Address+47] != 0x0B ) ||
       (in_data[PW_Start_Address+48] != 0x20 ) ||
       (in_data[PW_Start_Address+49] != 0x3A ) ||
       (in_data[PW_Start_Address+50] != 0xFF ) ||
       (in_data[PW_Start_Address+51] != 0xD8 ) ||
       (in_data[PW_Start_Address+52] != 0xD3 ) ||
       (in_data[PW_Start_Address+53] != 0xC0 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+10]*256*256*256) +
           (in_data[PW_Start_Address+11]*256*256) +
           (in_data[PW_Start_Address+12]*256) +
           in_data[PW_Start_Address+13] );

  PW_l += 648;

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



void Rip_SpikeCruncher ( void )
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
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = 0x96;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Save_Rip_Special ( "Spike Cruncher Exe-file", SpikeCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Spike Cruncher Exe-file", SpikeCruncher );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 40);  /* 36 should do but call it "just to be sure" :) */
}

