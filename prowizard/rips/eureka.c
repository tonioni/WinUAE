#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_EUREKA ( void )
{
  /* PW_l is still the sample data address */

  /*PW_WholeSampleSize is already the whole sample size */
  /*for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+in_data[PW_Start_Address+43+PW_j*30])*2);*/

  OutputSize = PW_WholeSampleSize + PW_l;

  /*  printf ( "\b\b\b\b\b\b\b\bEureka Packed module file found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Eureka_packer][0];
  OutName[2] = Extensions[Eureka_packer][1];
  OutName[3] = Extensions[Eureka_packer][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Eureka Packed module", Eureka_packer );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 46);  /* 45 should do but call it "just to be sure" :) */
}
