#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTimeCruncher17 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+22] != 0x72 ) ||
       (in_data[PW_Start_Address+23] != 0x03 ) ||
       (in_data[PW_Start_Address+24] != 0x61 ) ||
       (in_data[PW_Start_Address+25] != 0x00 ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0xFC ) ||
       (in_data[PW_Start_Address+28] != 0x4A ) ||
       (in_data[PW_Start_Address+29] != 0x02 ) ||
       (in_data[PW_Start_Address+30] != 0x67 ) ||
       (in_data[PW_Start_Address+31] != 0x5A ) ||
       (in_data[PW_Start_Address+32] != 0x0C ) ||
       (in_data[PW_Start_Address+33] != 0x42 ) ||
       (in_data[PW_Start_Address+34] != 0x00 ) ||
       (in_data[PW_Start_Address+35] != 0x07 ) ||
       (in_data[PW_Start_Address+36] != 0x66 ) ||
       (in_data[PW_Start_Address+37] != 0x24 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+6]*256*256*256) +
           (in_data[PW_Start_Address+7]*256*256) +
           (in_data[PW_Start_Address+8]*256) +
           in_data[PW_Start_Address+9] );

  PW_l += 376;

  if ( PW_i >= 36 )
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
