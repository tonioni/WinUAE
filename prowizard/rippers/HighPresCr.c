/* testHighPressureCruncher()    */
/* Rip_HighPressureCruncher()    */

#include "globals.h"
#include "extern.h"


int16_t	 testHighPressureCruncher ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+23] != 0x84 ) ||
       (in_data[PW_Start_Address+24] != 0x28 ) ||
       (in_data[PW_Start_Address+25] != 0x00 ) ||
       (in_data[PW_Start_Address+26] != 0xD5 ) ||
       (in_data[PW_Start_Address+27] != 0xC4 ) ||
       (in_data[PW_Start_Address+28] != 0x61 ) ||
       (in_data[PW_Start_Address+29] != 0x00 ) ||
       (in_data[PW_Start_Address+30] != 0x00 ) ||
       (in_data[PW_Start_Address+31] != 0x7C ) ||
       (in_data[PW_Start_Address+32] != 0xD3 ) ||
       (in_data[PW_Start_Address+33] != 0xC0 ) ||
       (in_data[PW_Start_Address+34] != 0x26 ) ||
       (in_data[PW_Start_Address+35] != 0x00 ) ||
       (in_data[PW_Start_Address+36] != 0x08 ) ||
       (in_data[PW_Start_Address+37] != 0x79 ) ||
       (in_data[PW_Start_Address+38] != 0x00 ) ||
       (in_data[PW_Start_Address+39] != 0x01 ) ||
       (in_data[PW_Start_Address+40] != 0x00 ) ||
       (in_data[PW_Start_Address+41] != 0xBF ) ||
       (in_data[PW_Start_Address+42] != 0xE0 ) ||
       (in_data[PW_Start_Address+43] != 0x01 ) ||
       (in_data[PW_Start_Address+44] != 0x18 ) )
  {
    /* should be enough :))) */
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+176]*256*256*256) +
           (in_data[PW_Start_Address+177]*256*256) +
           (in_data[PW_Start_Address+178]*256) +
           in_data[PW_Start_Address+179] );

  PW_l += 548;
  /*printf ( "testHighPressureCruncher():%ld (start:%ld)",PW_l,PW_Start_Address );*/

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


void Rip_HighPressureCruncher ( void )
{
  /* PW_l is still the whole size */

  uint8_t * Amiga_EXE_Header_Block;
  uint8_t * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 36;
    Amiga_EXE_Header_Block = (uint8_t *) malloc ( 36 );
    BZERO ( Amiga_EXE_Header_Block , 36 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[30] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x02;
    Amiga_EXE_Header_Block[13] = Amiga_EXE_Header_Block[27] = 0x01;
    Amiga_EXE_Header_Block[31] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 60;
    PW_j /= 4;
    Whatever = (uint8_t *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = *Whatever;

    Save_Rip_Special ( "High Pressure Cruncher Exe-file", HighPresCruncher, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "High Pressure Cruncher Exe-file", HighPresCruncher );
  }

  if ( Save_Status == GOOD )
    PW_i += 1;
}
