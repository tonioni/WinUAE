#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testTetrapack102 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+20] != 0x20 ) ||
       (in_data[PW_Start_Address+21] != 0x20 ) ||
       (in_data[PW_Start_Address+22] != 0xE2 ) ||
       (in_data[PW_Start_Address+23] != 0x88 ) ||
       (in_data[PW_Start_Address+24] != 0x66 ) ||
       (in_data[PW_Start_Address+25] != 0x02 ) ||
       (in_data[PW_Start_Address+26] != 0x61 ) ||
       (in_data[PW_Start_Address+27] != 0x34 ) ||
       (in_data[PW_Start_Address+28] != 0x65 ) ||
       (in_data[PW_Start_Address+29] != 0x7E ) ||
       (in_data[PW_Start_Address+30] != 0x72 ) ||
       (in_data[PW_Start_Address+32] != 0x76 ) ||
       (in_data[PW_Start_Address+33] != 0x01 ) )
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

  PW_l += 268;


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

