#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTNMCruncher11 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x4B ) ||
       (in_data[PW_Start_Address+17] != 0xFA ) ||
       (in_data[PW_Start_Address+18] != 0x01 ) ||
       (in_data[PW_Start_Address+19] != 0x9A ) ||
       (in_data[PW_Start_Address+20] != 0x41 ) ||
       (in_data[PW_Start_Address+21] != 0xFA ) ||
       (in_data[PW_Start_Address+22] != 0xFF ) ||
       (in_data[PW_Start_Address+23] != 0xE6 ) ||
       (in_data[PW_Start_Address+24] != 0x20 ) ||
       (in_data[PW_Start_Address+25] != 0x50 ) ||
       (in_data[PW_Start_Address+26] != 0xD1 ) ||
       (in_data[PW_Start_Address+27] != 0xC8 ) ||
       (in_data[PW_Start_Address+28] != 0xD1 ) ||
       (in_data[PW_Start_Address+29] != 0xC8 ) ||
       (in_data[PW_Start_Address+30] != 0x22 ) ||
       (in_data[PW_Start_Address+31] != 0x50 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+428]*256*256*256) +
           (in_data[PW_Start_Address+429]*256*256) +
           (in_data[PW_Start_Address+430]*256) +
           in_data[PW_Start_Address+431] );

  PW_l += 680;

  /* unpacked size */
  PW_m = ( (in_data[PW_Start_Address+432]*256*256*256) +
           (in_data[PW_Start_Address+433]*256*256) +
           (in_data[PW_Start_Address+434]*256) +
           in_data[PW_Start_Address+435] );


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
    else
      Amiga_EXE_Header = GOOD;
  }
  else
    Amiga_EXE_Header = BAD;

  return GOOD;
  /* PW_l is the size of the pack */
}

