#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testDefjam32 ( void )
{
  PW_Start_Address = PW_i-2;

  if ( (in_data[PW_Start_Address+12] != 0x28 ) ||
       (in_data[PW_Start_Address+13] != 0x7A ) ||
       (in_data[PW_Start_Address+14] != 0x01 ) ||
       (in_data[PW_Start_Address+15] != 0x52 ) ||
       (in_data[PW_Start_Address+16] != 0x20 ) ||
       (in_data[PW_Start_Address+17] != 0x4C ) ||
       (in_data[PW_Start_Address+18] != 0xD1 ) ||
       (in_data[PW_Start_Address+19] != 0xFC ) ||
       (in_data[PW_Start_Address+24] != 0xB3 ) ||
       (in_data[PW_Start_Address+25] != 0xCC ) ||
       (in_data[PW_Start_Address+26] != 0x6E ) ||
       (in_data[PW_Start_Address+27] != 0x08 ) ||
       (in_data[PW_Start_Address+28] != 0x20 ) ||
       (in_data[PW_Start_Address+29] != 0x49 ) ||
       (in_data[PW_Start_Address+30] != 0xD1 ) ||
       (in_data[PW_Start_Address+31] != 0xFA ) ||
       (in_data[PW_Start_Address+32] != 0xFF ) ||
       (in_data[PW_Start_Address+33] != 0xF4 ) ||
       (in_data[PW_Start_Address+34] != 0x60 ) ||
       (in_data[PW_Start_Address+35] != 0x06 ) ||
       (in_data[PW_Start_Address+36] != 0x18 ) ||
       (in_data[PW_Start_Address+37] != 0xD9 ) ||
       (in_data[PW_Start_Address+38] != 0xB9 ) ||
       (in_data[PW_Start_Address+39] != 0xC8 ) ||
       (in_data[PW_Start_Address+40] != 0x6D ) ||
       (in_data[PW_Start_Address+41] != 0xFA ) ||
       (in_data[PW_Start_Address+42] != 0x43 ) ||
       (in_data[PW_Start_Address+43] != 0xF9 ) ||
       (in_data[PW_Start_Address+44] != 0x00 ) )
  {
    /* should be enough :))) */
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+20]*256*256*256) +
           (in_data[PW_Start_Address+21]*256*256) +
           (in_data[PW_Start_Address+22]*256) +
           in_data[PW_Start_Address+23] );

  PW_l += 692;

  if ( PW_i >= 32 )
  {
    if ( (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x03 ) ||
         (in_data[PW_Start_Address-29]  != 0xF3 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x00 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x01 ) ||
         (in_data[PW_Start_Address-20]  != 0x00 ) ||
         (in_data[PW_Start_Address-19]  != 0x00 ) ||
         (in_data[PW_Start_Address-18]  != 0x00 ) ||
         (in_data[PW_Start_Address-17]  != 0x00 ) )
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

