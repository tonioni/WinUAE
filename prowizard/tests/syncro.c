#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSyncroPacker ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x45 ) ||
       (in_data[PW_Start_Address+17] != 0xFA ) ||
       (in_data[PW_Start_Address+18] != 0x01 ) ||
       (in_data[PW_Start_Address+19] != 0x35 ) ||
       (in_data[PW_Start_Address+20] != 0x14 ) ||
       (in_data[PW_Start_Address+21] != 0xA0 ) ||
       (in_data[PW_Start_Address+22] != 0x61 ) ||
       (in_data[PW_Start_Address+23] != 0x3E ) ||
       (in_data[PW_Start_Address+24] != 0x67 ) ||
       (in_data[PW_Start_Address+25] != 0x34 ) ||
       (in_data[PW_Start_Address+26] != 0x61 ) ||
       (in_data[PW_Start_Address+27] != 0x3A ) ||
       (in_data[PW_Start_Address+28] != 0x66 ) ||
       (in_data[PW_Start_Address+29] != 0x28 ) ||
       (in_data[PW_Start_Address+30] != 0x70 ) ||
       (in_data[PW_Start_Address+31] != 0x01 ) ||
       (in_data[PW_Start_Address+32] != 0x61 ) ||
       (in_data[PW_Start_Address+33] != 0x36 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+364]*256*256*256) +
           (in_data[PW_Start_Address+365]*256*256) +
           (in_data[PW_Start_Address+366]*256) +
           in_data[PW_Start_Address+367] );

  PW_l += 405;
  PW_m = (PW_l/4)*4;
  if ( PW_m != PW_l )
    PW_l = PW_m + 4;

  if ( ((PW_l - 32)+PW_Start_Address) > PW_in_size )
  {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


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
