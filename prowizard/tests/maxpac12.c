#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


short testMaxPacker12 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x01 ) ||
       (in_data[PW_Start_Address+17] != 0x70 ) ||
       (in_data[PW_Start_Address+18] != 0xD7 ) ||
       (in_data[PW_Start_Address+19] != 0xFA ) ||
       (in_data[PW_Start_Address+20] != 0x01 ) ||
       (in_data[PW_Start_Address+21] != 0x70 ) ||
       (in_data[PW_Start_Address+22] != 0x49 ) ||
       (in_data[PW_Start_Address+23] != 0xFA ) ||
       (in_data[PW_Start_Address+24] != 0x01 ) ||
       (in_data[PW_Start_Address+25] != 0x60 ) ||
       (in_data[PW_Start_Address+26] != 0x34 ) ||
       (in_data[PW_Start_Address+27] != 0x1C ) ||
       (in_data[PW_Start_Address+28] != 0x12 ) ||
       (in_data[PW_Start_Address+29] != 0x1C ) ||
       (in_data[PW_Start_Address+30] != 0x10 ) ||
       (in_data[PW_Start_Address+31] != 0x1C ) ||
       (in_data[PW_Start_Address+32] != 0x2C ) ||
       (in_data[PW_Start_Address+33] != 0x4B ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+384]*256*256*256) +
           (in_data[PW_Start_Address+385]*256*256) +
           (in_data[PW_Start_Address+386]*256) +
           in_data[PW_Start_Address+387] );

  PW_l += 429;
  PW_m = (PW_l/4)*4;
  if ( PW_m != PW_l )
    PW_l = PW_m + 4;


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

