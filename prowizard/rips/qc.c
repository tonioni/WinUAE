#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_QuadraComposer ( void )
{
  PW_l = (in_data[PW_Start_Address+4]*256*256*256)+
         (in_data[PW_Start_Address+5]*256*256)+
         (in_data[PW_Start_Address+6]*256)+
          in_data[PW_Start_Address+7];

  OutputSize = PW_l + 8;

  /*  printf ( "\b\b\b\b\b\b\b\bQuadra Composer module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[QuadraComposer][0];
  OutName[2] = Extensions[QuadraComposer][1];
  OutName[3] = Extensions[QuadraComposer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Quadra Composer module", QuadraComposer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 9);  /* 8 should do but call it "just to be sure" :) */
}

