#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testByteKiller_13 ( void )
{
  /*  if ( PW_i < 135 )*/
  if ( test_1_start (135) == BAD )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }

  PW_Start_Address = PW_i-135;

  if ( (in_data[PW_Start_Address]    != 0x41 ) ||
       (in_data[PW_Start_Address+1]  != 0xFA ) ||
       (in_data[PW_Start_Address+2]  != 0x00 ) ||
       (in_data[PW_Start_Address+3]  != 0xE6 ) ||
       (in_data[PW_Start_Address+4]  != 0x43 ) ||
       (in_data[PW_Start_Address+5]  != 0xF9 ) ||
       (in_data[PW_Start_Address+10] != 0x20 ) ||
       (in_data[PW_Start_Address+11] != 0x18 ) ||
       (in_data[PW_Start_Address+12] != 0x22 ) ||
       (in_data[PW_Start_Address+13] != 0x18 ) ||
       (in_data[PW_Start_Address+14] != 0x2A ) ||
       (in_data[PW_Start_Address+15] != 0x18 ) ||
       (in_data[PW_Start_Address+16] != 0x24 ) ||
       (in_data[PW_Start_Address+17] != 0x49 ) ||
       (in_data[PW_Start_Address+18] != 0xD1 ) ||
       (in_data[PW_Start_Address+19] != 0xC0 ) ||
       (in_data[PW_Start_Address+20] != 0xD5 ) ||
       (in_data[PW_Start_Address+21] != 0xC1 ) ||
       (in_data[PW_Start_Address+22] != 0x20 ) ||
       (in_data[PW_Start_Address+23] != 0x20 ) ||
       (in_data[PW_Start_Address+24] != 0xB1 ) ||
       (in_data[PW_Start_Address+25] != 0x85 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+232]*256*256*256) +
           (in_data[PW_Start_Address+233]*256*256) +
           (in_data[PW_Start_Address+234]*256) +
           in_data[PW_Start_Address+235] );

  PW_l += 304;

  if ( PW_i >= 171 )
  {
    if ( (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) ||
         (in_data[PW_Start_Address-34]  != 0x03 ) ||
         (in_data[PW_Start_Address-33]  != 0xF3 ) ||
         (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x00 ) ||
         (in_data[PW_Start_Address-29]  != 0x00 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x02 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x00 ) )
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

