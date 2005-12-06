#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_DoubleAction10 ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;

  OutputSize = PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bDouble Action 1.0 Exe-file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Double_Action][0];
  OutName[2] = Extensions[Double_Action][1];
  OutName[3] = Extensions[Double_Action][2];*/

  CONVERT = BAD;
  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 24;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 24 );
    BZERO ( Amiga_EXE_Header_Block , 24 );
    Amiga_EXE_Header_Block[2]  = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x01;
    Amiga_EXE_Header_Block[20] = in_data[PW_Start_Address+4];
    Amiga_EXE_Header_Block[21] = in_data[PW_Start_Address+5];
    Amiga_EXE_Header_Block[22] = in_data[PW_Start_Address+6];
    Amiga_EXE_Header_Block[23] = in_data[PW_Start_Address+7];

    Save_Rip_Special ( "Double Action 1.0 Exe-file", Double_Action, Amiga_EXE_Header_Block , 24 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 24;
    Save_Rip ( "Double Action 1.0 Exe-file", Double_Action );
  }

  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 149);  /* 147 should do but call it "just to be sure" :) */
}

