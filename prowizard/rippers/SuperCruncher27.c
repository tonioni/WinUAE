/* testSuperCruncher27() */
/* Rip_SuperCruncher27() */


#include "globals.h"
#include "extern.h"


short testSuperCruncher27 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0xFF ) ||
       (in_data[PW_Start_Address+17] != 0xE8 ) ||
       (in_data[PW_Start_Address+18] != 0x22 ) ||
       (in_data[PW_Start_Address+19] != 0x68 ) ||
       (in_data[PW_Start_Address+20] != 0x00 ) ||
       (in_data[PW_Start_Address+21] != 0x04 ) ||
       (in_data[PW_Start_Address+22] != 0x42 ) ||
       (in_data[PW_Start_Address+23] != 0xA8 ) ||
       (in_data[PW_Start_Address+24] != 0x00 ) ||
       (in_data[PW_Start_Address+25] != 0x04 ) ||
       (in_data[PW_Start_Address+26] != 0xD3 ) ||
       (in_data[PW_Start_Address+27] != 0xC9 ) ||
       (in_data[PW_Start_Address+28] != 0xD3 ) ||
       (in_data[PW_Start_Address+29] != 0xC9 ) ||
       (in_data[PW_Start_Address+30] != 0x59 ) ||
       (in_data[PW_Start_Address+31] != 0x89 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+680]*256*256*256) +
           (in_data[PW_Start_Address+681]*256*256) +
           (in_data[PW_Start_Address+682]*256) +
           in_data[PW_Start_Address+683] );

  PW_l *= 4;
  PW_l += 724;

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




void Rip_SuperCruncher27 ( void )
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
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = 0xA8;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Amiga_EXE_Header_Block[24] = in_data[PW_Start_Address+680];
    Amiga_EXE_Header_Block[25] = in_data[PW_Start_Address+681];
    Amiga_EXE_Header_Block[26] = in_data[PW_Start_Address+682];
    Amiga_EXE_Header_Block[27] = in_data[PW_Start_Address+683];

    /* also the last 4 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "Super Cruncher 2.7 Exe-file", SuperCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Super Cruncher 2.7 Exe-file", SuperCruncher );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 40);  /* 36 should do but call it "just to be sure" :) */
}

