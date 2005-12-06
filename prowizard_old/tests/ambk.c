/* (31 mar 2003)
 * ambk.c (test)
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testAmBk ( void )
{
  if (PW_i + 68 > PW_in_size)
  {
    return BAD;
  }

  /* test #1  */
  PW_Start_Address = PW_i;
  if ((in_data[PW_Start_Address+4] != 0x00)||
      (in_data[PW_Start_Address+5] != 0x03)||
      (in_data[PW_Start_Address+6] != 0x00)||
      (in_data[PW_Start_Address+7] >  0x01)||
      (in_data[PW_Start_Address+12]!= 'M')||
      (in_data[PW_Start_Address+13]!= 'u')||
      (in_data[PW_Start_Address+14]!= 's')||
      (in_data[PW_Start_Address+15]!= 'i')||
      (in_data[PW_Start_Address+16]!= 'c')||
      (in_data[PW_Start_Address+17]!= ' ')||
      (in_data[PW_Start_Address+18]!= ' ')||
      (in_data[PW_Start_Address+19]!= ' '))
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* get the whole size */
  PW_k = (in_data[PW_Start_Address+9]*256*256)+(in_data[PW_Start_Address+10]*256)+in_data[PW_Start_Address+11]+12;
  if ( PW_k+PW_Start_Address > PW_in_size )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}

