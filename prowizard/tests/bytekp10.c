#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testbytekillerpro10 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+22] != 0x00 ) ||
       (in_data[PW_Start_Address+23] != 0x04 ) ||
       (in_data[PW_Start_Address+24] != 0x2A ) ||
       (in_data[PW_Start_Address+25] != 0x28 ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x08 ) ||
       (in_data[PW_Start_Address+28] != 0x41 ) ||
       (in_data[PW_Start_Address+29] != 0xE8 ) ||
       (in_data[PW_Start_Address+30] != 0x00 ) ||
       (in_data[PW_Start_Address+31] != 0x0C ) ||
       (in_data[PW_Start_Address+32] != 0x24 ) ||
       (in_data[PW_Start_Address+33] != 0x49 ) ||
       (in_data[PW_Start_Address+34] != 0xD1 ) ||
       (in_data[PW_Start_Address+35] != 0xC0 ) ||
       (in_data[PW_Start_Address+36] != 0xD5 ) ||
       (in_data[PW_Start_Address+37] != 0xC1 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+222]*256*256*256) +
           (in_data[PW_Start_Address+223]*256*256) +
           (in_data[PW_Start_Address+224]*256) +
           in_data[PW_Start_Address+225] );

  PW_l += 308;


  if ( PW_i >= 50 )
  {
    if ( (in_data[PW_Start_Address-50]  != 0x00 ) ||
         (in_data[PW_Start_Address-49]  != 0x00 ) ||
         (in_data[PW_Start_Address-48]  != 0x03 ) ||
         (in_data[PW_Start_Address-47]  != 0xF3 ) ||
         (in_data[PW_Start_Address-46]  != 0x00 ) ||
         (in_data[PW_Start_Address-45]  != 0x00 ) ||
         (in_data[PW_Start_Address-44]  != 0x00 ) ||
         (in_data[PW_Start_Address-43]  != 0x00 ) ||
         (in_data[PW_Start_Address-42]  != 0x00 ) ||
         (in_data[PW_Start_Address-41]  != 0x00 ) ||
         (in_data[PW_Start_Address-40]  != 0x00 ) ||
         (in_data[PW_Start_Address-39]  != 0x02 ) ||
         (in_data[PW_Start_Address-38]  != 0x00 ) ||
         (in_data[PW_Start_Address-37]  != 0x00 ) ||
         (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;

  return GOOD;
  /* PW_l is the size of the pack */
}

