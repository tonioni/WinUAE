#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Powerpacker23 ( void )
{
  /* PW_l is still the whole size */


  Uchar * Amiga_EXE_Header_Block;

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bPowerpacker 2.3 Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Powerpacker23][0];
  OutName[2] = Extensions[Powerpacker23][1];
  OutName[3] = Extensions[Powerpacker23][2];*/

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
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = 0x83;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Save_Rip_Special ( "Powerpacker 2.3 Exe-file", Powerpacker23,  Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Powerpacker 2.3 Exe-file", Powerpacker23 );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 40);  /* 36 should do but call it "just to be sure" :) */
}

