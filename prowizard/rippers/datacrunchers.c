/* testArcDDataCruncher() */
/* testCRND() */

#include "globals.h"
#include "extern.h"


short testArcDDataCruncher ( void )
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


short testCRND ( void )
{
  PW_Start_Address = PW_i;

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+4]*256*256*256) +
           (in_data[PW_Start_Address+5]*256*256) +
           (in_data[PW_Start_Address+6]*256) +
           in_data[PW_Start_Address+7] );

  if ( (PW_l + PW_i) > PW_in_size )
    return BAD;

  testSpecialCruncherData ( 4 , in_data[PW_l-16] );

  return GOOD;
}
