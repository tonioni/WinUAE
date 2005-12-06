#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testArcDDataCruncher ()
{
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 12) > PW_in_size )
  {
    return BAD;
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+8]*256*256*256) +
           (in_data[PW_Start_Address+9]*256*256) +
           (in_data[PW_Start_Address+10]*256) +
           in_data[PW_Start_Address+11] );
  /* unpacked size */
  PW_k = ( (in_data[PW_Start_Address+5]*256*256) +
           (in_data[PW_Start_Address+6]*256) +
           in_data[PW_Start_Address+7] );

  if ( (PW_k <= 2) || (PW_l <= 2) )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }

  if ( PW_l > 0xFFFFFF )
  {
/*printf ( "#2\n" );*/
    return BAD;
  }

  if ( PW_k <= PW_l )
  {
/*printf ( "#3\n" );*/
    return BAD;
  }

  return GOOD;
}
