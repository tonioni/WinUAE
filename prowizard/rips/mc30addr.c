#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_MasterCruncher30addr ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bMaster Cruncher 3.0 address Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[MasterCruncher][0];
  OutName[2] = Extensions[MasterCruncher][1];
  OutName[3] = Extensions[MasterCruncher][2];*/

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
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = 0x6A;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Amiga_EXE_Header_Block[24] = in_data[PW_Start_Address+428];
    Amiga_EXE_Header_Block[25] = in_data[PW_Start_Address+429];
    Amiga_EXE_Header_Block[26] = in_data[PW_Start_Address+430];
    Amiga_EXE_Header_Block[27] = in_data[PW_Start_Address+431];
    Save_Rip_Special ( "Master Cruncher 3.0 address Exe-file", MasterCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Master Cruncher 3.0 address Exe-file", MasterCruncher );
  }

  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 38);  /* 36 should do but call it "just to be sure" :) */
}

