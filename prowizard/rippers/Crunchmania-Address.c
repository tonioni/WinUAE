/* testcrunchmaniaAddr() */
/* Rip_CrunchmaniaAddr() */

#include "globals.h"
#include "extern.h"


short testcrunchmaniaAddr ( void )
{
  /*PW_Start_Address = PW_i - 4;*/
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+32] != 0x6F ) ||
       (in_data[PW_Start_Address+33] != 0x14 ) ||
       (in_data[PW_Start_Address+34] != 0x26 ) ||
       (in_data[PW_Start_Address+35] != 0x4A ) ||
       (in_data[PW_Start_Address+36] != 0x49 ) ||
       (in_data[PW_Start_Address+37] != 0xE9 ) ||
       (in_data[PW_Start_Address+42] != 0xE4 ) ||
       (in_data[PW_Start_Address+43] != 0x8F ) ||
       (in_data[PW_Start_Address+44] != 0x52 ) ||
       (in_data[PW_Start_Address+45] != 0x87 ) ||
       (in_data[PW_Start_Address+46] != 0x24 ) ||
       (in_data[PW_Start_Address+47] != 0x4C ) ||
       (in_data[PW_Start_Address+48] != 0x28 ) ||
       (in_data[PW_Start_Address+49] != 0xDB ) ||
       (in_data[PW_Start_Address+50] != 0x53 ) ||
       (in_data[PW_Start_Address+51] != 0x87 ) ||
       (in_data[PW_Start_Address+52] != 0x66 ) ||
       (in_data[PW_Start_Address+53] != 0xFA ) )
  { /* another case ...*/
    if ( (in_data[PW_Start_Address+36] != 0x22 ) ||
	 (in_data[PW_Start_Address+37] != 0x1A ) ||
	 (in_data[PW_Start_Address+38] != 0x24 ) ||
	 (in_data[PW_Start_Address+39] != 0x1A ) ||
	 (in_data[PW_Start_Address+40] != 0x47 ) ||
	 (in_data[PW_Start_Address+41] != 0xEA ) )
    {
      /* should be enough :))) */
      /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }


  /* packed size */
  PW_j = ( (in_data[PW_Start_Address+6]*256) +
	   in_data[PW_Start_Address+7] )+10;

  PW_l = ( (in_data[PW_Start_Address+PW_j]*256*256*256) +
	   (in_data[PW_Start_Address+PW_j+1]*256*256) +
	   (in_data[PW_Start_Address+PW_j+2]*256) +
	   in_data[PW_Start_Address+PW_j+3] );

  PW_l += (40 + PW_j);
  if ((PW_l%4) != 0)
    PW_l += 2;


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
    Save_Rip_Special ( "Crunchmania Address Exe-file", CRM1, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 32;
    Save_Rip ( "Crunchmania Address Exe-file", CRM1 );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 16);
}
