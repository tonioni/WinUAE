#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSTC300 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x45 ) ||
       (in_data[PW_Start_Address+17] != 0xfa ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x58 ) ||
       (in_data[PW_Start_Address+20] != 0x3d ) ||
       (in_data[PW_Start_Address+21] != 0x5a ) ||
       (in_data[PW_Start_Address+22] != 0x00 ) ||
       (in_data[PW_Start_Address+23] != 0x04 ) ||
       (in_data[PW_Start_Address+24] != 0x18 ) ||
       (in_data[PW_Start_Address+25] != 0xbc ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x81 ) ||
       (in_data[PW_Start_Address+28] != 0x3c ) ||
       (in_data[PW_Start_Address+29] != 0x9a ) ||
       (in_data[PW_Start_Address+30] != 0x18 ) ||
       (in_data[PW_Start_Address+31] != 0xbc ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+340]*256*256*256) +
           (in_data[PW_Start_Address+341]*256*256) +
           (in_data[PW_Start_Address+342]*256) +
           in_data[PW_Start_Address+343] );

  PW_l += 392;


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

