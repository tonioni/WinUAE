#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPMZ ( void )
{
  PW_Start_Address = PW_i;

  /* test 1 */
  if ( (PW_Start_Address + 4456) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  if ( in_data[PW_Start_Address + 21] != 0xd2 )
  {
    return BAD;
  }

  /* test 3 */
  PW_j = (in_data[PW_Start_Address+4456]*256*256*256)+(in_data[PW_Start_Address+4457]*256*256)+(in_data[PW_Start_Address+4458]*256)+in_data[PW_Start_Address+4459];
  if ( (PW_Start_Address + PW_j + 4456) > PW_in_size )
  {
    return BAD;
  }

  /* test 4 */
  PW_k = (in_data[PW_Start_Address+4712]*256)+in_data[PW_Start_Address+4713];
  PW_l = PW_k/4;
  PW_l *= 4;
  if ( PW_l != PW_k )
  {
    return BAD;
  }

  /* test 5 */
  if ( in_data[PW_Start_Address + 36] != 0x11 )
  {
    return BAD;
  }

  /* test 6 */
  if ( in_data[PW_Start_Address + 37] != 0x00 )
  {
    return BAD;
  }

  return GOOD;
}

