/* testMegaCruncherObj() */
/* Rip_MegaCruncherObj() */

#include "globals.h"
#include "extern.h"


short testMegaCruncherObj ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+12] != 0x4C ) ||
       (in_data[PW_Start_Address+13] != 0xDD ) ||
       (in_data[PW_Start_Address+14] != 0x00 ) ||
       (in_data[PW_Start_Address+15] != 0x03 ) ||
       (in_data[PW_Start_Address+16] != 0x4E ) ||
       (in_data[PW_Start_Address+17] != 0xAE ) ||
       (in_data[PW_Start_Address+18] != 0xFF ) ||
       (in_data[PW_Start_Address+19] != 0x3A ) ||
       (in_data[PW_Start_Address+20] != 0x4A ) ||
       (in_data[PW_Start_Address+21] != 0x80 ) ||
       (in_data[PW_Start_Address+22] != 0x67 ) ||
       (in_data[PW_Start_Address+23] != 0x30 ) ||
       (in_data[PW_Start_Address+24] != 0x41 ) ||
       (in_data[PW_Start_Address+25] != 0xFA ) ||
       (in_data[PW_Start_Address+26] != 0x00 ) ||
       (in_data[PW_Start_Address+27] != 0x0E ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }

  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+466]*256*256*256) +
	   (in_data[PW_Start_Address+467]*256*256) +
	   (in_data[PW_Start_Address+468]*256) +
	   in_data[PW_Start_Address+469] );
  PW_l += 532;
  
  if ((PW_l > PW_in_size) || (PW_l > 2000000l))
  {
    return BAD;
  }

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



void Rip_MegaCruncherObj ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 36;
    Amiga_EXE_Header_Block = (Uchar *) malloc ( 36 );
    BZERO ( Amiga_EXE_Header_Block , 36 );
    Amiga_EXE_Header_Block[2]  = Amiga_EXE_Header_Block[30] = 0x03;
    Amiga_EXE_Header_Block[3]  = 0xF3;
    Amiga_EXE_Header_Block[11] = 0x02;
    Amiga_EXE_Header_Block[19] = 0x01;
    Amiga_EXE_Header_Block[27] = 0x01;
    Amiga_EXE_Header_Block[31] = 0xE9;

    /* WARNING !!! WORKS ONLY ON PC !!!       */
    /* 68k machines code : c1 = *(Whatever+2); */
    /* 68k machines code : c2 = *(Whatever+3); */
    PW_j = PW_l - 204;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = *Whatever;

    /* also the last 16 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-16] = 0x00;
    in_data[PW_Start_Address+OutputSize-15] = 0x00;
    in_data[PW_Start_Address+OutputSize-14] = 0x03;
    in_data[PW_Start_Address+OutputSize-13] = 0xF2;

    in_data[PW_Start_Address+OutputSize-12] = 0x00;
    in_data[PW_Start_Address+OutputSize-11] = 0x00;
    in_data[PW_Start_Address+OutputSize-10] = 0x03;
    in_data[PW_Start_Address+OutputSize-9]  = 0xEB;

    in_data[PW_Start_Address+OutputSize-8]  = 0x00;
    in_data[PW_Start_Address+OutputSize-7]  = 0x00;
    in_data[PW_Start_Address+OutputSize-6]  = 0x00;
    in_data[PW_Start_Address+OutputSize-5]  = 0x01;

    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "Mega Cruncher Obj", MegaCruncherObj, Amiga_EXE_Header_Block , 36 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 36;
    Save_Rip ( "Mega Cruncher Obj", MegaCruncherObj );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 40; /* beside header */
}
