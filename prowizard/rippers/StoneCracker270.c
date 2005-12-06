/* testStoneCracker270() */
/* Rip_StoneCracker270() */


#include "globals.h"
#include "extern.h"


short testStoneCracker270 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x1E ) ||
       (in_data[PW_Start_Address+17] != 0x3A ) ||
       (in_data[PW_Start_Address+18] != 0x01 ) ||
       (in_data[PW_Start_Address+19] != 0x57 ) ||
       (in_data[PW_Start_Address+20] != 0x1A ) ||
       (in_data[PW_Start_Address+21] != 0x3A ) ||
       (in_data[PW_Start_Address+22] != 0x01 ) ||
       (in_data[PW_Start_Address+23] != 0x60 ) ||
       (in_data[PW_Start_Address+24] != 0x1C ) ||
       (in_data[PW_Start_Address+25] != 0x3A ) ||
       (in_data[PW_Start_Address+26] != 0x01 ) ||
       (in_data[PW_Start_Address+27] != 0x5D ) ||
       (in_data[PW_Start_Address+28] != 0x41 ) ||
       (in_data[PW_Start_Address+29] != 0xFA ) ||
       (in_data[PW_Start_Address+30] != 0x01 ) ||
       (in_data[PW_Start_Address+31] != 0x5A ) ||
       (in_data[PW_Start_Address+32] != 0xD1 ) ||
       (in_data[PW_Start_Address+33] != 0xFA ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+370]*256*256*256) +
           (in_data[PW_Start_Address+371]*256*256) +
           (in_data[PW_Start_Address+372]*256) +
           in_data[PW_Start_Address+373] );

  PW_l += 412;


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




void Rip_StoneCracker270 ( void )
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
    Save_Rip_Special ( "StoneCracker 2.70 Exe-file", StoneCracker270, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 32;
    Save_Rip ( "StoneCracker 2.70 Exe-file", StoneCracker270 );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 36);  /* 32 should do but call it "just to be sure" :) */
}

