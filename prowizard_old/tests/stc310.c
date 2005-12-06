#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSTC310 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x3D ) ||
       (in_data[PW_Start_Address+17] != 0x58 ) ||
       (in_data[PW_Start_Address+18] != 0xff ) ||
       (in_data[PW_Start_Address+19] != 0x1a ) ||
       (in_data[PW_Start_Address+20] != 0x3d ) ||
       (in_data[PW_Start_Address+21] != 0x58 ) ||
       (in_data[PW_Start_Address+22] != 0xff ) ||
       (in_data[PW_Start_Address+23] != 0x16 ) ||
       (in_data[PW_Start_Address+24] != 0x16 ) ||
       (in_data[PW_Start_Address+25] != 0xbc ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x81 ) ||
       (in_data[PW_Start_Address+28] != 0x45 ) ||
       (in_data[PW_Start_Address+29] != 0xfa ) ||
       (in_data[PW_Start_Address+30] != 0x01 ) ||
       (in_data[PW_Start_Address+31] != 0xea ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+524]*256*256*256) +
           (in_data[PW_Start_Address+525]*256*256) +
           (in_data[PW_Start_Address+526]*256) +
           in_data[PW_Start_Address+527] );

  PW_l += 564;


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

