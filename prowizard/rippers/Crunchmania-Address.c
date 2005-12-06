/* testcrunchmaniaAddr() */
/* Rip_CrunchmaniaAddr() */

#include "globals.h"
#include "extern.h"


short testcrunchmaniaAddr ( void )
{
  PW_Start_Address = PW_i - 4;

  if ( (in_data[PW_Start_Address+36] != 0x6F ) ||
       (in_data[PW_Start_Address+37] != 0x14 ) ||
       (in_data[PW_Start_Address+38] != 0x26 ) ||
       (in_data[PW_Start_Address+39] != 0x4A ) ||
       (in_data[PW_Start_Address+40] != 0x49 ) ||
       (in_data[PW_Start_Address+41] != 0xE9 ) ||
       (in_data[PW_Start_Address+46] != 0xE4 ) ||
       (in_data[PW_Start_Address+47] != 0x8F ) ||
       (in_data[PW_Start_Address+48] != 0x52 ) ||
       (in_data[PW_Start_Address+49] != 0x87 ) ||
       (in_data[PW_Start_Address+50] != 0x24 ) ||
       (in_data[PW_Start_Address+51] != 0x4C ) ||
       (in_data[PW_Start_Address+52] != 0x28 ) ||
       (in_data[PW_Start_Address+53] != 0xDB ) ||
       (in_data[PW_Start_Address+54] != 0x53 ) ||
       (in_data[PW_Start_Address+55] != 0x87 ) ||
       (in_data[PW_Start_Address+56] != 0x66 ) ||
       (in_data[PW_Start_Address+57] != 0xFA ) )
  {
    /* should be enough :))) */
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address]*256*256*256) +
           (in_data[PW_Start_Address+1]*256*256) +
           (in_data[PW_Start_Address+2]*256) +
           in_data[PW_Start_Address+3] );

  PW_l *= 4;
  PW_l += 36;


  if ( PW_i >= 28 )
  {
    if ( (in_data[PW_Start_Address-28]  != 0x00 ) ||
         (in_data[PW_Start_Address-27]  != 0x00 ) ||
         (in_data[PW_Start_Address-26]  != 0x03 ) ||
         (in_data[PW_Start_Address-25]  != 0xF3 ) ||
         (in_data[PW_Start_Address-24]  != 0x00 ) ||
         (in_data[PW_Start_Address-23]  != 0x00 ) ||
         (in_data[PW_Start_Address-22]  != 0x00 ) ||
         (in_data[PW_Start_Address-21]  != 0x00 ) ||
         (in_data[PW_Start_Address-20]  != 0x00 ) ||
         (in_data[PW_Start_Address-19]  != 0x00 ) ||
         (in_data[PW_Start_Address-18]  != 0x00 ) ||
         (in_data[PW_Start_Address-17]  != 0x01 ) ||
         (in_data[PW_Start_Address-16]  != 0x00 ) ||
         (in_data[PW_Start_Address-15]  != 0x00 ) ||
         (in_data[PW_Start_Address-14]  != 0x00 ) ||
         (in_data[PW_Start_Address-13]  != 0x00 ) )
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


void Rip_CrunchmaniaAddr ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 32;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 32 );
    BZERO ( Amiga_EXE_Header_Block , 32 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[26] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x01;
    Amiga_EXE_Header_Block[27] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 36;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[28] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[29] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[30] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[31] = *Whatever;
    PW_Start_Address += 4;
    Save_Rip_Special ( "Crunchmania Address Exe-file", CRM1, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 28;
    Save_Rip ( "Crunchmania Address Exe-file", CRM1 );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 16);
}
