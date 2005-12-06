/* testTetrapack102() */
/* Rip_Tetrapack102() */

#include "globals.h"
#include "extern.h"


short testTetrapack102 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+20] != 0x20 ) ||
       (in_data[PW_Start_Address+21] != 0x20 ) ||
       (in_data[PW_Start_Address+22] != 0xE2 ) ||
       (in_data[PW_Start_Address+23] != 0x88 ) ||
       (in_data[PW_Start_Address+24] != 0x66 ) ||
       (in_data[PW_Start_Address+25] != 0x02 ) ||
       (in_data[PW_Start_Address+26] != 0x61 ) ||
       (in_data[PW_Start_Address+27] != 0x34 ) ||
       (in_data[PW_Start_Address+28] != 0x65 ) ||
       (in_data[PW_Start_Address+29] != 0x7E ) ||
       (in_data[PW_Start_Address+30] != 0x72 ) ||
       (in_data[PW_Start_Address+32] != 0x76 ) ||
       (in_data[PW_Start_Address+33] != 0x01 ) )
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

  PW_l += 268;


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




void Rip_Tetrapack102 ( void )
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
    Save_Rip_Special ( "Tetrapack 1.02 Exe-file", TPACK102, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 32;
    Save_Rip ( "Tetrapack 1.02 Exe-file", TPACK102 );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 36);  /* 32 should do but call it "just to be sure" :) */
}
