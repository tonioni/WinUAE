#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testMasterCruncher30addr ( void )
{

  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0xD3 ) ||
       (in_data[PW_Start_Address+17] != 0xC9 ) ||
       (in_data[PW_Start_Address+18] != 0x58 ) ||
       (in_data[PW_Start_Address+19] != 0x89 ) ||
       (in_data[PW_Start_Address+20] != 0x2B ) ||
       (in_data[PW_Start_Address+21] != 0x49 ) ||
       (in_data[PW_Start_Address+124]!= 0xE3 ) ||
       (in_data[PW_Start_Address+125]!= 0x10 ) ||
       (in_data[PW_Start_Address+126]!= 0xE3 ) ||
       (in_data[PW_Start_Address+127]!= 0x51 ) ||
       (in_data[PW_Start_Address+128]!= 0x51 ) ||
       (in_data[PW_Start_Address+129]!= 0xCA ) ||
       (in_data[PW_Start_Address+130]!= 0xFF ) ||
       (in_data[PW_Start_Address+131]!= 0xF4 ) ||
       (in_data[PW_Start_Address+132]!= 0x4A ) ||
       (in_data[PW_Start_Address+133]!= 0x43 ) ||
       (in_data[PW_Start_Address+134]!= 0x67 ) ||
       (in_data[PW_Start_Address+135]!= 0x06 ) )
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

  PW_l *= 4;
  PW_l += 472;

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

