#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testDoubleAction10 ( void )
{
  if ( PW_i < 123 )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }
  PW_Start_Address = PW_i-123;

  if ( (in_data[PW_Start_Address+8]  != 0x47 ) ||
       (in_data[PW_Start_Address+9]  != 0xF9 ) ||
       (in_data[PW_Start_Address+10] != 0x00 ) ||
       (in_data[PW_Start_Address+11] != 0xDF ) ||
       (in_data[PW_Start_Address+12] != 0xF0 ) ||
       (in_data[PW_Start_Address+13] != 0x00 ) ||
       (in_data[PW_Start_Address+14] != 0x37 ) ||
       (in_data[PW_Start_Address+15] != 0x7C ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x9A ) ||
       (in_data[PW_Start_Address+26] != 0x43 ) ||
       (in_data[PW_Start_Address+27] != 0xF9 ) ||
       (in_data[PW_Start_Address+28] != 0x00 ) ||
       (in_data[PW_Start_Address+29] != 0xBF ) ||
       (in_data[PW_Start_Address+30] != 0xD1 ) ||
       (in_data[PW_Start_Address+31] != 0x00 ) ||
       (in_data[PW_Start_Address+32] != 0x12 ) ||
       (in_data[PW_Start_Address+33] != 0xBC ) ||
       (in_data[PW_Start_Address+34] != 0x00 ) ||
       (in_data[PW_Start_Address+35] != 0xFD ) )
  {
    /* should be enough :))) */
printf ( "#2 Start:%ld\n" , PW_Start_Address );
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+4]*256*256*256) +
           (in_data[PW_Start_Address+5]*256*256) +
           (in_data[PW_Start_Address+6]*256) +
           in_data[PW_Start_Address+7] );
  PW_l *= 4;
  PW_l += 36;

  if ( PW_i >= 147 )
  {
    if ( (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x03 ) ||
         (in_data[PW_Start_Address-21]  != 0xF3 ) ||
         (in_data[PW_Start_Address-20]  != 0x00 ) ||
         (in_data[PW_Start_Address-19]  != 0x00 ) ||
         (in_data[PW_Start_Address-18]  != 0x00 ) ||
         (in_data[PW_Start_Address-17]  != 0x00 ) ||
         (in_data[PW_Start_Address-16]  != 0x00 ) ||
         (in_data[PW_Start_Address-15]  != 0x00 ) ||
         (in_data[PW_Start_Address-14]  != 0x00 ) ||
         (in_data[PW_Start_Address-13]  != 0x01 ) ||
         (in_data[PW_Start_Address-12]  != 0x00 ) ||
         (in_data[PW_Start_Address-11]  != 0x00 ) ||
         (in_data[PW_Start_Address-10]  != 0x00 ) ||
         (in_data[PW_Start_Address-9]   != 0x00 ) )
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
