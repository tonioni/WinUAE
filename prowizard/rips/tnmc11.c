#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_TNMCruncher11 ( void )
{
  /* PW_l is still the whole size */
  /* PW_m is the decrunched size  (necessary to rebuild header) */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bTNM Cruncher 1.1 Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[TNMCruncher][0];
  OutName[2] = Extensions[TNMCruncher][1];
  OutName[3] = Extensions[TNMCruncher][2];*/

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
