#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTurboSqueezer61 ( void )
{

  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+12] != 0xFF ) ||
       (in_data[PW_Start_Address+13] != 0xF0 ) ||
       (in_data[PW_Start_Address+14] != 0xD1 ) ||
       (in_data[PW_Start_Address+15] != 0xC8 ) ||
       (in_data[PW_Start_Address+16] != 0xD1 ) ||
       (in_data[PW_Start_Address+17] != 0xC8 ) ||
       (in_data[PW_Start_Address+18] != 0x22 ) ||
       (in_data[PW_Start_Address+19] != 0x58 ) ||
       (in_data[PW_Start_Address+20] != 0x28 ) ||
       (in_data[PW_Start_Address+21] != 0x48 ) ||
       (in_data[PW_Start_Address+22] != 0xD3 ) ||
       (in_data[PW_Start_Address+23] != 0xC9 ) ||
       (in_data[PW_Start_Address+24] != 0xD3 ) ||
       (in_data[PW_Start_Address+25] != 0xC9 ) ||
       (in_data[PW_Start_Address+26] != 0x58 ) ||
       (in_data[PW_Start_Address+27] != 0x89 ) ||
       (in_data[PW_Start_Address+28] != 0x2A ) ||
       (in_data[PW_Start_Address+29] != 0x49 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+484]*256*256*256) +
           (in_data[PW_Start_Address+485]*256*256) +
           (in_data[PW_Start_Address+486]*256) +
           in_data[PW_Start_Address+487] );

  PW_l += 572;
  /*
  if ( PW_i >= 40 )
  {
    if ( (in_data[PW_Start_Address-40]  != 0x00 ) ||
         (in_data[PW_Start_Address-39]  != 0x00 ) ||
         (in_data[PW_Start_Address-38]  != 0x03 ) ||
         (in_data[PW_Start_Address-37]  != 0xF3 ) ||
         (in_data[PW_Start_Address-36]  != 0x00 ) ||
         (in_data[PW_Start_Address-35]  != 0x00 ) ||
         (in_data[PW_Start_Address-34]  != 0x00 ) ||
         (in_data[PW_Start_Address-33]  != 0x00 ) ||
         (in_data[PW_Start_Address-32]  != 0x00 ) ||
         (in_data[PW_Start_Address-31]  != 0x00 ) ||
         (in_data[PW_Start_Address-30]  != 0x00 ) ||
         (in_data[PW_Start_Address-29]  != 0x03 ) ||
         (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x00 ) ||
         (in_data[PW_Start_Address-25]  != 0x00 ) )
    {
      Amiga_EXE_Header = BAD;
    }
    else*/
      Amiga_EXE_Header = GOOD;
      /*}
  else
  Amiga_EXE_Header = BAD;*/


  return GOOD;
  /* PW_l is the size of the pack */
}

