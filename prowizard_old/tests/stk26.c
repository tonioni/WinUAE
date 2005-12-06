#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


/* Soundtracker 2.6 & IceTracker 1.0 */
short testSTK26 ( void )
{
  /* test 1 */
  if ( PW_i < 1464 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1464;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+45+30*PW_j] > 0x40 )
    {
/*      printf ( "#1\n" );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+44+30*PW_j] > 0x0F )
    {
/*      printf ( "#2\n" );*/
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+
                            in_data[PW_Start_Address+43+PW_j*30])*2);
  }

  /* PW_WholeSampleSize is the whole sample size :) */
  return GOOD;
}

