#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testHQCCruncher2 ( void )
{
  PW_Start_Address = PW_i - 64;

  if ( (in_data[PW_Start_Address+1776] != 0x02 ) ||
       (in_data[PW_Start_Address+1777] != 0x4D ) ||
       (in_data[PW_Start_Address+1778] != 0x45 ) ||
       (in_data[PW_Start_Address+1779] != 0x58 ) ||
       (in_data[PW_Start_Address+1780] != 0x00 ) ||
       (in_data[PW_Start_Address+1781] != 0x00 ) ||
       (in_data[PW_Start_Address+1782] != 0x03 ) ||
       (in_data[PW_Start_Address+1783] != 0xEC ) ||
       (in_data[PW_Start_Address+1784] != 0x00 ) ||
       (in_data[PW_Start_Address+1785] != 0x00 ) ||
       (in_data[PW_Start_Address+1786] != 0x00 ) ||
       (in_data[PW_Start_Address+1787] != 0x00 ) ||
       (in_data[PW_Start_Address+1788] != 0x00 ) ||
       (in_data[PW_Start_Address+1789] != 0x00 ) ||
       (in_data[PW_Start_Address+1790] != 0x03 ) ||
       (in_data[PW_Start_Address+1791] != 0xF2 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+1808]*256*256*256) +
           (in_data[PW_Start_Address+1809]*256*256) +
           (in_data[PW_Start_Address+1810]*256) +
           in_data[PW_Start_Address+1811] );

  PW_l *= 4;
  PW_l += 1816;


/*  if ( PW_i >= 50 ) */
/*  { */
/*    if ( (in_data[PW_Start_Address-50]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-49]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-48]  != 0x03 ) || */
/*         (in_data[PW_Start_Address-47]  != 0xF3 ) || */
/*         (in_data[PW_Start_Address-46]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-45]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-44]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-43]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-42]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-41]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-40]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-39]  != 0x02 ) || */
/*         (in_data[PW_Start_Address-38]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-37]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-36]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-35]  != 0x00 ) ) */
/*    { */
/*      Amiga_EXE_Header = BAD; */
/*    } */
/*    else */
      Amiga_EXE_Header = GOOD;
/*  } */
/*  else */
/*    Amiga_EXE_Header = BAD; */

  return GOOD;
  /* PW_l is the size of the pack */
}

