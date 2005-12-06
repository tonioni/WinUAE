#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_FC13 ( void )
{
  /* PW_m is the addy of the 1st sample */

  /* whole sample size */
  PW_WholeSampleSize =((in_data[PW_Start_Address+36]*256*256*256)+
                       (in_data[PW_Start_Address+37]*256*256)+
                       (in_data[PW_Start_Address+38]*256)+
                        in_data[PW_Start_Address+39] );

  OutputSize = PW_WholeSampleSize + PW_m;

  CONVERT = BAD;
  Save_Rip ( "Future Composer 1.3 module", FC13 );
  
  if ( Save_Status == GOOD )
    PW_i += 4; /* after SMOD tag */
}

