#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPowerpacker4lib ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+92] != 'p' ) ||
       (in_data[PW_Start_Address+93] != 'o' ) ||
       (in_data[PW_Start_Address+94] != 'w' ) ||
       (in_data[PW_Start_Address+95] != 'e' ) ||
       (in_data[PW_Start_Address+96] != 'r' ) ||
       (in_data[PW_Start_Address+97] != 'p' ) ||
       (in_data[PW_Start_Address+98] != 'a' ) ||
       (in_data[PW_Start_Address+99] != 'c' ) ||
       (in_data[PW_Start_Address+100]!= 'k' ) ||
       (in_data[PW_Start_Address+101]!= 'e' ) ||
       (in_data[PW_Start_Address+102]!= 'r' ) ||
       (in_data[PW_Start_Address+103]!= '.' ) ||
       (in_data[PW_Start_Address+104]!= 'l' ) ||
       (in_data[PW_Start_Address+105]!= 'i' ) ||
       (in_data[PW_Start_Address+106]!= 'b' ) ||
       (in_data[PW_Start_Address+107]!= 'r' ) ||
       (in_data[PW_Start_Address+108]!= 'a' ) ||
       (in_data[PW_Start_Address+109]!= 'r' ) ||
       (in_data[PW_Start_Address+110]!= 'y' ) )
  {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+32]*256*256*256) +
           (in_data[PW_Start_Address+33]*256*256) +
           (in_data[PW_Start_Address+34]*256) +
           in_data[PW_Start_Address+35] );

  PW_l += 176;

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
