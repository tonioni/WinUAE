#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


short testPM10c ( void )
{
  /* test 1 */
  if ( (PW_Start_Address + 4452) > PW_in_size )
  {
    return BAD;
  }

  /* test 2 */
  if ( in_data[PW_Start_Address + 21] != 0xce )
  {
    return BAD;
  }

  /* test 3 */
  PW_j = (in_data[PW_Start_Address+4452]*256*256*256)+(in_data[PW_Start_Address+4453]*256*256)+(in_data[PW_Start_Address+4454]*256)+in_data[PW_Start_Address+4455];
  if ( (PW_Start_Address + PW_j + 4452) > PW_in_size )
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
  if ( in_data[PW_Start_Address + 36] != 0x10 )
  {
    return BAD;
  }

  /* test 6 */
  if ( in_data[PW_Start_Address + 37] != 0xFC )
  {
    return BAD;
  }

  return GOOD;
}

