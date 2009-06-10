/* testTNMCruncher11() */
/* Rip_TNMCruncher11() */


#include "globals.h"
#include "extern.h"


short testTNMCruncher11 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x4B ) ||
       (in_data[PW_Start_Address+17] != 0xFA ) ||
       (in_data[PW_Start_Address+18] != 0x01 ) ||
       (in_data[PW_Start_Address+19] != 0x9A ) ||
       (in_data[PW_Start_Address+20] != 0x41 ) ||
       (in_data[PW_Start_Address+21] != 0xFA ) ||
       (in_data[PW_Start_Address+22] != 0xFF ) ||
       (in_data[PW_Start_Address+23] != 0xE6 ) ||
       (in_data[PW_Start_Address+24] != 0x20 ) ||
       (in_data[PW_Start_Address+25] != 0x50 ) ||
       (in_data[PW_Start_Address+26] != 0xD1 ) ||
       (in_data[PW_Start_Address+27] != 0xC8 ) ||
       (in_data[PW_Start_Address+28] != 0xD1 ) ||
       (in_data[PW_Start_Address+29] != 0xC8 ) ||
       (in_data[PW_Start_Address+30] != 0x22 ) ||
       (in_data[PW_Start_Address+31] != 0x50 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+428]*256*256*256) +
           (in_data[PW_Start_Address+429]*256*256) +
           (in_data[PW_Start_Address+430]*256) +
           in_data[PW_Start_Address+431] );

  PW_l += 680;

  /* unpacked size */
  PW_m = ( (in_data[PW_Start_Address+432]*256*256*256) +
           (in_data[PW_Start_Address+433]*256*256) +
           (in_data[PW_Start_Address+434]*256) +
           in_data[PW_Start_Address+435] );


  if ( PW_i >= 40 )
  {
    if ( (in_data[PW_Start_Address-40]  != 0x00 ) ||
         (in_data[PW_Start_Address-39]  != 0x00 ) ||
         (in_data[PW_Start_Address-38]  != 0x03 ) ||
         (in_data[PW_Start_Address-37]  != 0xF3 ) ||
         (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) ||
         (in_data[PW_Start_Address-34]  != 0x00 ) ||
         (in_data[PW_Start_Address-33]  != 0x00 ) ||
         (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x00 ) ||
         (in_data[PW_Start_Address-29]  != 0x03 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x00 ) )
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




void Rip_TNMCruncher11 ( void )
{
  /* PW_l is still the whole size */
  /* PW_m is the decrunched size  (necessary to rebuild header) */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 40;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 40 );
    BZERO ( Amiga_EXE_Header_Block , 40 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[34] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x03;
    Amiga_EXE_Header_Block[19] = 0x02;
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[39] = 0x9B;
    Amiga_EXE_Header_Block[35] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 680;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[24] = *(Whatever+3);
    Amiga_EXE_Header_Block[25] = *(Whatever+2);
    Amiga_EXE_Header_Block[26] = *(Whatever+1);
    Amiga_EXE_Header_Block[27] = *Whatever;
    PW_j = PW_m / 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[28] = *(Whatever+3);
    Amiga_EXE_Header_Block[29] = *(Whatever+2);
    Amiga_EXE_Header_Block[30] = *(Whatever+1);
    Amiga_EXE_Header_Block[31] = *Whatever;

    /* also the last 12 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-12] = 0x00;
    in_data[PW_Start_Address+OutputSize-11] = 0x00;
    in_data[PW_Start_Address+OutputSize-10] = 0x03;
    in_data[PW_Start_Address+OutputSize-9]  = 0xEB;

    in_data[PW_Start_Address+OutputSize-8]  = *(Whatever+3);
    in_data[PW_Start_Address+OutputSize-7]  = *(Whatever+2);
    in_data[PW_Start_Address+OutputSize-6]  = *(Whatever+1);
    in_data[PW_Start_Address+OutputSize-5]  = *Whatever;

    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "TNM Cruncher 1.1 Exe-file", TNMCruncher, Amiga_EXE_Header_Block , 40 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 40;
    Save_Rip ( "TNM Cruncher 1.1 Exe-file", TNMCruncher );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 44);  /* 40 should do but call it "just to be sure" :) */
}
