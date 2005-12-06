#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_FC14 ( void )
{
  /* PW_l is already the 1st waveform addy */

  /* get Waveforms len */
  PW_k = 0;
  for ( PW_j=100 ; PW_j<180 ; PW_j++ )
  {
    PW_k += in_data[PW_Start_Address+PW_j];
  }

  OutputSize = PW_l + (PW_k*2);

  CONVERT = BAD;
  Save_Rip ( "Future Composer 1.4 module", FC14 );
  
  if ( Save_Status == GOOD )
    PW_i += 4; /* after FC14 tag */
}

