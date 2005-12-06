/* testMegaCruncher10() */
/* testMegaCruncher12() */
/* Rip_MegaCruncher() */

#include "globals.h"
#include "extern.h"


short testMegaCruncher10 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x22 ) ||
       (in_data[PW_Start_Address+17] != 0x6B ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x04 ) ||
       (in_data[PW_Start_Address+20] != 0x24 ) ||
       (in_data[PW_Start_Address+21] != 0x60 ) ||
       (in_data[PW_Start_Address+22] != 0xD5 ) ||
       (in_data[PW_Start_Address+23] != 0xC9 ) ||
       (in_data[PW_Start_Address+24] != 0x20 ) ||
       (in_data[PW_Start_Address+25] != 0x20 ) ||
       (in_data[PW_Start_Address+26] != 0x72 ) ||
       (in_data[PW_Start_Address+27] != 0x03 ) ||
       (in_data[PW_Start_Address+28] != 0x61 ) ||
       (in_data[PW_Start_Address+29] != 0x00 ) ||
       (in_data[PW_Start_Address+30] != 0x01 ) ||
       (in_data[PW_Start_Address+31] != 0x02 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+312]*256*256*256) +
           (in_data[PW_Start_Address+313]*256*256) +
           (in_data[PW_Start_Address+314]*256) +
           in_data[PW_Start_Address+315] );

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


short testMegaCruncher12 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x22 ) ||
       (in_data[PW_Start_Address+17] != 0x6B ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x04 ) ||
       (in_data[PW_Start_Address+20] != 0x24 ) ||
       (in_data[PW_Start_Address+21] != 0x60 ) ||
       (in_data[PW_Start_Address+22] != 0xD5 ) ||
       (in_data[PW_Start_Address+23] != 0xC9 ) ||
       (in_data[PW_Start_Address+24] != 0x20 ) ||
       (in_data[PW_Start_Address+25] != 0x20 ) ||
       (in_data[PW_Start_Address+26] != 0x72 ) ||
       (in_data[PW_Start_Address+27] != 0x03 ) ||
       (in_data[PW_Start_Address+28] != 0x61 ) ||
       (in_data[PW_Start_Address+29] != 0x00 ) ||
       (in_data[PW_Start_Address+30] != 0x01 ) ||
       (in_data[PW_Start_Address+31] != 0x06 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+316]*256*256*256) +
           (in_data[PW_Start_Address+317]*256*256) +
           (in_data[PW_Start_Address+318]*256) +
           in_data[PW_Start_Address+319] );

  PW_l += 380;

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



void Rip_MegaCruncher ( void )
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
    Save_Rip_Special ( "Mega Cruncher 1.0/1.2 Exe-file", MegaCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Mega Cruncher 1.0/1.2 Exe-file", MegaCruncher );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 42);  /* 36 should do but call it "just to be sure" :) */
}

