#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testRelokIt10 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x00 ) ||
       (in_data[PW_Start_Address+17] != 0x00 ) ||
       (in_data[PW_Start_Address+18] != 0x01 ) ||
       (in_data[PW_Start_Address+19] != 0xF0 ) ||
       (in_data[PW_Start_Address+20] != 0x23 ) ||
       (in_data[PW_Start_Address+21] != 0xE8 ) ||
       (in_data[PW_Start_Address+22] != 0x00 ) ||
       (in_data[PW_Start_Address+23] != 0x06 ) ||
       (in_data[PW_Start_Address+24] != 0x00 ) ||
       (in_data[PW_Start_Address+25] != 0x00 ) ||
       (in_data[PW_Start_Address+26] != 0x01 ) ||
       (in_data[PW_Start_Address+27] != 0xEC ) ||
       (in_data[PW_Start_Address+28] != 0x20 ) ||
       (in_data[PW_Start_Address+29] != 0x28 ) ||
       (in_data[PW_Start_Address+30] != 0x00 ) ||
       (in_data[PW_Start_Address+31] != 0x0E ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  PW_j = in_data[PW_Start_Address+721];
  PW_l = ( (in_data[PW_Start_Address+734+(PW_j*8)]*256*256*256) +
	   (in_data[PW_Start_Address+735+(PW_j*8)]*256*256) +
	   (in_data[PW_Start_Address+736+(PW_j*8)]*256) +
	   in_data[PW_Start_Address+737+(PW_j*8)] );
  PW_l += (952 + (PW_j*8));

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

