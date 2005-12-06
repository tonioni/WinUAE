#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_STARPACK ( void )
{
  /* PW_k is still the sample data address */
  /* PW_WholeSampleSize is the whole sample size already */

  OutputSize = PW_WholeSampleSize + PW_k + 0x314;

  CONVERT = GOOD;
  Save_Rip ( "StarTrekker Packer module", Star_pack );
  
  if ( Save_Status == GOOD )
    PW_i += 24;  /* 23 after 1st vol */
}

