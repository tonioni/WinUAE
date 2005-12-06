#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_TurboSqueezer61 ( void )
{
  /* PW_l is still the whole size */

  /*Uchar * Amiga_EXE_Header_Block;*/
  /*Uchar * Whatever;*/

  OutputSize = PW_l;

  CONVERT = BAD;
  /*
  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 40;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 40 );
    BZERO ( Amiga_EXE_Header_Block , 40 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[11] = Amiga_EXE_Header_Block[34] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[19] = 0x02;
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[39] = 0x7D;
    Amiga_EXE_Header_Block[35] = 0xE9;
  */
    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
  /*    PW_j = PW_l - 568;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[24] = *(Whatever+3);
    Amiga_EXE_Header_Block[25] = *(Whatever+2);
    Amiga_EXE_Header_Block[26] = *(Whatever+1);
    Amiga_EXE_Header_Block[27] = *Whatever;

    PW_j = ((in_data[PW_Start_Address+480]*256*256*256)+
	    (in_data[PW_Start_Address+481]*256*256)+
	    (in_data[PW_Start_Address+482]*256)+
	    in_data[PW_Start_Address+483]) + 36;

    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[28] = *(Whatever+3);
    Amiga_EXE_Header_Block[29] = *(Whatever+2);
    Amiga_EXE_Header_Block[30] = *(Whatever+1);
    Amiga_EXE_Header_Block[31] = *Whatever;
  */
    /* also the last 4 bytes are 'removed' frequently ... Here they are */
  /*    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "TurboSqueezer 6.1 Exe-file", TurboSqueezer61, Amiga_EXE_Header_Block , 40 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {*/
    PW_Start_Address -= 40;
    Save_Rip ( "TurboSqueezer 6.1 Exe-file", TurboSqueezer61 );
    /*  }*/

  if ( Save_Status == GOOD )
    PW_i += 44;
}
