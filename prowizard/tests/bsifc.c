#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testBSIFutureComposer ( void )
{
  PW_Start_Address = PW_i;

  /* file size < 18424 */
  if ( test_1_start(PW_Start_Address + 18424) )
  {
/*printf ( "#1 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
    return BAD;
  }

  if (( in_data[PW_Start_Address+17412] != 'D' ) ||
      ( in_data[PW_Start_Address+17413] != 'I' ) ||
      ( in_data[PW_Start_Address+17414] != 'G' ) ||
      ( in_data[PW_Start_Address+17415] != 'I' ) )
  {
/*printf ( "#2 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
     return BAD;
  }

  if (( in_data[PW_Start_Address+18424] != 'D' ) ||
      ( in_data[PW_Start_Address+18425] != 'I' ) ||
      ( in_data[PW_Start_Address+18426] != 'G' ) ||
      ( in_data[PW_Start_Address+18427] != 'P' ) )
  {
/*printf ( "#3 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
     return BAD;
  }

  return GOOD;
}

