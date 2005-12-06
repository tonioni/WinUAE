#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPPbk ( void )
{
  PW_Start_Address = PW_i;

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+12]*256*256*256) +
           (in_data[PW_Start_Address+13]*256*256) +
           (in_data[PW_Start_Address+14]*256) +
           in_data[PW_Start_Address+15] );

  if ( PW_l > PW_in_size )
  {
/*printf ( "#2 Start:%ld (header size:%ld) (in_size:%ld)\n" , PW_Start_Address,PW_l , PW_in_size);*/
    return BAD;
  }


  /* PP20 packed size */
  PW_k = ( (in_data[PW_Start_Address+20+PW_l]*256*256) +
           (in_data[PW_Start_Address+21+PW_l]*256) +
            in_data[PW_Start_Address+22+PW_l]);
  /* PP20 packed size */
  PW_m = ( (in_data[PW_Start_Address+8]*256*256*256) +
           (in_data[PW_Start_Address+9]*256*256) +
           (in_data[PW_Start_Address+10]*256) +
           in_data[PW_Start_Address+11] );

  if ( PW_m != PW_k )
  {
/*printf ( "#2 Start:%ld (PP20 size:%ld) (Header size:%ld)\n" , PW_Start_Address , PW_k , PW_m );*/
    return BAD;
  }

  PW_l += 8;
  return GOOD;
  /* PW_l is the size of the pack (PP20 subfile size !)*/
}

