#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_DigiBooster17 ( void )
{
  /* PW_m is the number of pattern saved -1 */
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_k is the module size saves the samples data */

  OutputSize = PW_WholeSampleSize + PW_k;

  /*  printf ( "\b\b\b\b\b\b\b\bDigiBooster 1.7 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[DigiBooster][0];
  OutName[2] = Extensions[DigiBooster][1];
  OutName[3] = Extensions[DigiBooster][2];*/

  CONVERT = BAD;
  Save_Rip ( "DigiBooster 1.7 module", DigiBooster );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}
