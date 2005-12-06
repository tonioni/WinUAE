#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


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

/*  testSpecialCruncherData ( 4 , in_data[(in_data[PW_i+4]*256*256*256+in_data[PW_i+5]*256*256+in_data[PW_i+6]*256+in_data[PW_i+7])-16] );*/
  testSpecialCruncherData ( 4 , in_data[PW_l-16] );

  return GOOD;
}
