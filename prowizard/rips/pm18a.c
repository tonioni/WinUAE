#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM18a ( void )
{
  /* we NEED this 'PW_j' value found while testing !,so we keep it :) */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+4464+8*PW_k]*256)+in_data[PW_Start_Address+4465+8*PW_k])*2);
  OutputSize = 4460 + PW_j + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bPromizer 1.8a module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Promizer_18a][0];
  OutName[2] = Extensions[Promizer_18a][1];
  OutName[3] = Extensions[Promizer_18a][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Promizer 1.8a module", Promizer_18a );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}

