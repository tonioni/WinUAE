#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_Polka ( void )
{
  /* PW_WholeSampleSize is the whole sample size */
  /* PW_k is the highest pattern number */

  OutputSize = PW_WholeSampleSize + (PW_k*1024) + 0x43c;

  CONVERT = GOOD;
  Save_Rip ( "Polka Packed music", PolkaPacker );
  
  if ( Save_Status == GOOD )
    PW_i += 0x43C;  /* put back pointer after header*/
}
