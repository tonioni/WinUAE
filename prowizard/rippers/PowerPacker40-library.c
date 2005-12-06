/* testPowerpacker4lib() */
/* Rip_Powerpacker4lib() */

#include "globals.h"
#include "extern.h"


short testPowerpacker4lib ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+92] != 'p' ) ||
       (in_data[PW_Start_Address+93] != 'o' ) ||
       (in_data[PW_Start_Address+94] != 'w' ) ||
       (in_data[PW_Start_Address+95] != 'e' ) ||
       (in_data[PW_Start_Address+96] != 'r' ) ||
       (in_data[PW_Start_Address+97] != 'p' ) ||
       (in_data[PW_Start_Address+98] != 'a' ) ||
       (in_data[PW_Start_Address+99] != 'c' ) ||
       (in_data[PW_Start_Address+100]!= 'k' ) ||
       (in_data[PW_Start_Address+101]!= 'e' ) ||
       (in_data[PW_Start_Address+102]!= 'r' ) ||
       (in_data[PW_Start_Address+103]!= '.' ) ||
       (in_data[PW_Start_Address+104]!= 'l' ) ||
       (in_data[PW_Start_Address+105]!= 'i' ) ||
       (in_data[PW_Start_Address+106]!= 'b' ) ||
       (in_data[PW_Start_Address+107]!= 'r' ) ||
       (in_data[PW_Start_Address+108]!= 'a' ) ||
       (in_data[PW_Start_Address+109]!= 'r' ) ||
       (in_data[PW_Start_Address+110]!= 'y' ) )
  {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+32]*256*256*256) +
           (in_data[PW_Start_Address+33]*256*256) +
           (in_data[PW_Start_Address+34]*256) +
           in_data[PW_Start_Address+35] );

  PW_l += 176;

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



void Rip_Powerpacker4lib ( void )
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

    /* also the last 4 bytes could be 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "Powerpacker 4.0 library Exe-file", Powerpacker4, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Powerpacker 4.0 library Exe-file", Powerpacker4 );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 37);  /* 36 should do but call it "just to be sure" :) */
}

