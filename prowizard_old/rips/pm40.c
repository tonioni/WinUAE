#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_PM40 ( void )
{
  /* PW_l is the sample data address */

  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+264+PW_k*8]*256)+in_data[PW_Start_Address+265+PW_k*8])*2);

  OutputSize = PW_WholeSampleSize + PW_l + 4;
  /*  printf ( "\b\b\b\b\b\b\b\bPromizer 4.0 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[PM40][0];
  OutName[2] = Extensions[PM40][1];
  OutName[3] = Extensions[PM40][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Promizer 4.0 module", PM40 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}

