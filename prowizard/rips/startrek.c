#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_StarTrekker ( void )
{
  /* PW_k is still the nbr of pattern */
  /* PW_WholeSampleSize is still the whole sample size */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 1084;

  CONVERT = BAD;
  Save_Rip ( "StarTrekker module", StarTrekker );
  
  if ( Save_Status == GOOD )
    PW_i += 1081;  /* after header */
}

