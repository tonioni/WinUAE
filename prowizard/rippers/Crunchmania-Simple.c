/* testcrunchmaniaSimple() */
/* Rip_CrunchmaniaSimple() */

#include "globals.h"
#include "extern.h"


short testcrunchmaniaSimple ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0xFF ) ||
       (in_data[PW_Start_Address+17] != 0xEC ) ||
       (in_data[PW_Start_Address+18] != 0xD9 ) ||
       (in_data[PW_Start_Address+19] != 0xCC ) ||
       (in_data[PW_Start_Address+20] != 0xD9 ) ||
       (in_data[PW_Start_Address+21] != 0xCC ) ||
       (in_data[PW_Start_Address+22] != 0x2A ) ||
       (in_data[PW_Start_Address+23] != 0x49 ) ||
       (in_data[PW_Start_Address+24] != 0xD3 ) ||
       (in_data[PW_Start_Address+25] != 0xC1 ) ||
       (in_data[PW_Start_Address+26] != 0xD5 ) ||
       (in_data[PW_Start_Address+27] != 0xC2 ) ||
       (in_data[PW_Start_Address+28] != 0x30 ) ||
       (in_data[PW_Start_Address+29] != 0x22 ) ||
       (in_data[PW_Start_Address+30] != 0x2C ) ||
       (in_data[PW_Start_Address+31] != 0x22 ) ||
       (in_data[PW_Start_Address+32] != 0x7E ) ||
       (in_data[PW_Start_Address+33] != 0x10 ) )
  {
    return BAD;
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+368]*256*256*256) +
	   (in_data[PW_Start_Address+369]*256*256) +
	   (in_data[PW_Start_Address+370]*256) +
	   in_data[PW_Start_Address+371] );

  PW_l += 422;

  if ( PW_i >= 36 )
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
         (in_data[PW_Start_Address-21]  != 0x02 ) ||
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


void Rip_CrunchmaniaSimple ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    /* packed size */
    PW_n = ( (in_data[PW_Start_Address+364]*256*256*256) +
	     (in_data[PW_Start_Address+365]*256*256) +
	     (in_data[PW_Start_Address+366]*256) +
	     in_data[PW_Start_Address+367] );

    OutputSize -= 36;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 36 );
    BZERO ( Amiga_EXE_Header_Block , 36 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[30] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[19] = 0x01;
    Amiga_EXE_Header_Block[31] = 0xE9;

    Amiga_EXE_Header_Block[24] = 0x40;
    Amiga_EXE_Header_Block[25] = in_data[PW_Start_Address+361];
    Amiga_EXE_Header_Block[26] = in_data[PW_Start_Address+362];
    Amiga_EXE_Header_Block[27] = in_data[PW_Start_Address+363];

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 48;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[35] = *Whatever;

    if ((PW_n%4) != 0)
      PW_n += 2;
    PW_n += 372;
    PW_n /= 4;
    Whatever = (Uchar *) &PW_n;
    Amiga_EXE_Header_Block[20] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = *Whatever;

    Save_Rip_Special ( "Crunchmania Simple Exe-file", CrunchmaniaSimple, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Crunchmania Simple Exe-file", CrunchmaniaSimple );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 0x66; /* after detection sequence */
}
