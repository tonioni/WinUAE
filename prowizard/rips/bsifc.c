#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_BSIFutureComposer ( void )
{
  /* get whole sample size */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<63 ; PW_j++ )
  {
    PW_o =((in_data[PW_Start_Address+17420+(PW_j*16)]*256*256*256)+
           (in_data[PW_Start_Address+17421+(PW_j*16)]*256*256)+
           (in_data[PW_Start_Address+17422+(PW_j*16)]*256)+
            in_data[PW_Start_Address+17423+(PW_j*16)] );
    PW_WholeSampleSize += PW_o;
  }

  OutputSize = PW_WholeSampleSize + 18428;

  /*  printf ( "\b\b\b\b\b\b\b\bBSI Future Composer module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[BSIFC][0];
  OutName[2] = Extensions[BSIFC][1];
  OutName[3] = Extensions[BSIFC][2];*/

  CONVERT = BAD;
  Save_Rip ( "BSI Future Composer module", BSIFC );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}
