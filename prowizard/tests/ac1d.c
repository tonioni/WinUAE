#ifdef DOS

#include "..\include\globals.h"
#include "..\include\extern.h"

#endif

#ifdef UNIX

#include "../include/globals.h"
#include "../include/extern.h"

#endif




short testAC1D ( void )
{
  /* test #1 */
  /*  if ( PW_i<2 )*/
  if ( test_1_start(2) == BAD )
    return BAD;

  /* test #2 */
  PW_Start_Address = PW_i-2;
  if ( (in_data[PW_Start_Address] > 0x7f) || ((PW_Start_Address+896)>PW_in_size) )
  {
    return BAD;
  }

  /* test #4 */
  for ( PW_k = 0 ; PW_k < 31 ; PW_k ++ )
  {
    if ( in_data[PW_Start_Address + 10 + (8*PW_k)] > 0x0f )
    {
      return BAD;
    }
  }

  /* test #5 */
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address + 768 + PW_j] > 0x7f )
    {
      return BAD;
    }
  }
  return GOOD;
}

