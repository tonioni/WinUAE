/* testTimeCruncher17() */
/* Rip_TimeCruncher17() */


#include "globals.h"
#include "extern.h"


short testTimeCruncher17 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+22] != 0x72 ) ||
       (in_data[PW_Start_Address+23] != 0x03 ) ||
       (in_data[PW_Start_Address+24] != 0x61 ) ||
       (in_data[PW_Start_Address+25] != 0x00 ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0xFC ) ||
       (in_data[PW_Start_Address+28] != 0x4A ) ||
       (in_data[PW_Start_Address+29] != 0x02 ) ||
       (in_data[PW_Start_Address+30] != 0x67 ) ||
       (in_data[PW_Start_Address+31] != 0x5A ) ||
       (in_data[PW_Start_Address+32] != 0x0C ) ||
       (in_data[PW_Start_Address+33] != 0x42 ) ||
       (in_data[PW_Start_Address+34] != 0x00 ) ||
       (in_data[PW_Start_Address+35] != 0x07 ) ||
       (in_data[PW_Start_Address+36] != 0x66 ) ||
       (in_data[PW_Start_Address+37] != 0x24 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+6]*256*256*256) +
           (in_data[PW_Start_Address+7]*256*256) +
           (in_data[PW_Start_Address+8]*256) +
           in_data[PW_Start_Address+9] );

  PW_l += 376;

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




void Rip_TimeCruncher17 ( void )
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

    /* WARNING !!! WORKS ONLY ON PC !!!        */
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

    Save_Rip_Special ( "Time Cruncher 1.7 Exe-file", TimeCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Time Cruncher 1.7 Exe-file", TimeCruncher );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 42);  /* 36 should do but call it "just to be sure" :) */
}
