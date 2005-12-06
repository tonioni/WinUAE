#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_MegaCruncher ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bMega Cruncher 1.0/1.2 Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
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

