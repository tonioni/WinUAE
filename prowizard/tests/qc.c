#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testQuadraComposer ( void )
{
  /* test #1 */
  if ( PW_i < 8 )
  {
/*printf ( "#1 (PW_i:%ld)\n" , PW_i );*/
    return BAD;
  }
  PW_Start_Address = PW_i-8;

  /* test #2 "FORM" & "EMIC" */
  if ( (in_data[PW_Start_Address]    != 'F') ||
       (in_data[PW_Start_Address+1]  != 'O') ||
       (in_data[PW_Start_Address+2]  != 'R') ||
       (in_data[PW_Start_Address+3]  != 'M') ||
       (in_data[PW_Start_Address+12] != 'E') ||
       (in_data[PW_Start_Address+13] != 'M') ||
       (in_data[PW_Start_Address+14] != 'I') ||
       (in_data[PW_Start_Address+15] != 'C') )
  {
/*printf ( "#2 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test number of samples */
  PW_l = in_data[PW_Start_Address+63];
  if ( (PW_l == 0x00) || (PW_l > 0x20) )
  {
/*printf ( "#3 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}


