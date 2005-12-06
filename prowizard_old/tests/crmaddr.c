#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testcrunchmaniaAddr ( void )
{
  PW_Start_Address = PW_i - 4;

  if ( (in_data[PW_Start_Address+36] != 0x6F ) ||
       (in_data[PW_Start_Address+37] != 0x14 ) ||
       (in_data[PW_Start_Address+38] != 0x26 ) ||
       (in_data[PW_Start_Address+39] != 0x4A ) ||
       (in_data[PW_Start_Address+40] != 0x49 ) ||
       (in_data[PW_Start_Address+41] != 0xE9 ) ||
       (in_data[PW_Start_Address+46] != 0xE4 ) ||
       (in_data[PW_Start_Address+47] != 0x8F ) ||
       (in_data[PW_Start_Address+48] != 0x52 ) ||
       (in_data[PW_Start_Address+49] != 0x87 ) ||
       (in_data[PW_Start_Address+50] != 0x24 ) ||
       (in_data[PW_Start_Address+51] != 0x4C ) ||
       (in_data[PW_Start_Address+52] != 0x28 ) ||
       (in_data[PW_Start_Address+53] != 0xDB ) ||
       (in_data[PW_Start_Address+54] != 0x53 ) ||
       (in_data[PW_Start_Address+55] != 0x87 ) ||
       (in_data[PW_Start_Address+56] != 0x66 ) ||
       (in_data[PW_Start_Address+57] != 0xFA ) )
  {
    /* should be enough :))) */
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address]*256*256*256) +
           (in_data[PW_Start_Address+1]*256*256) +
           (in_data[PW_Start_Address+2]*256) +
           in_data[PW_Start_Address+3] );

  PW_l *= 4;
  PW_l += 36;


  if ( PW_i >= 28 )
  {
    if ( (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x03 ) ||
         (in_data[PW_Start_Address-25]  != 0xF3 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x00 ) ||
         (in_data[PW_Start_Address-20]  != 0x00 ) ||
         (in_data[PW_Start_Address-19]  != 0x00 ) ||
         (in_data[PW_Start_Address-18]  != 0x00 ) ||
         (in_data[PW_Start_Address-17]  != 0x01 ) ||
         (in_data[PW_Start_Address-16]  != 0x00 ) ||
         (in_data[PW_Start_Address-15]  != 0x00 ) ||
         (in_data[PW_Start_Address-14]  != 0x00 ) ||
         (in_data[PW_Start_Address-13]  != 0x00 ) )
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

