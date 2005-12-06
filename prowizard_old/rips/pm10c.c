#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM10c ( void )
{
  /* we NEED this 'PW_j' value found while testing !,so we keep it :) */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+4460+8*PW_k]*256)+in_data[PW_Start_Address+4461+8*PW_k])*2);
  OutputSize = 4456 + PW_j + PW_WholeSampleSize;
  /*  printf ( "\b\b\b\b\b\b\b\bPromizer 1.0c module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Promizer_10c][0];
  OutName[2] = Extensions[Promizer_10c][1];
  OutName[3] = Extensions[Promizer_10c][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Promizer 1.0c module", Promizer_10c );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}

