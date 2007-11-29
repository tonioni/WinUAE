/* testTryIt101() */
/* Rip_TryIt101() */


#include "globals.h"
#include "extern.h"


short testTryIt101 ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x22 ) ||
       (in_data[PW_Start_Address+17] != 0x2D ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x10 ) ||
       (in_data[PW_Start_Address+20] != 0x10 ) ||
       (in_data[PW_Start_Address+21] != 0xD9 ) ||
       (in_data[PW_Start_Address+22] != 0x53 ) ||
       (in_data[PW_Start_Address+23] != 0x81 ) ||
       (in_data[PW_Start_Address+24] != 0x66 ) ||
       (in_data[PW_Start_Address+25] != 0xFA ) ||
       (in_data[PW_Start_Address+26] != 0x22 ) ||
       (in_data[PW_Start_Address+27] != 0x4A ) ||
       (in_data[PW_Start_Address+28] != 0x20 ) ||
       (in_data[PW_Start_Address+29] != 0x2D ) ||
       (in_data[PW_Start_Address+30] != 0x00 ) ||
       (in_data[PW_Start_Address+31] != 0x14 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+358]*256*256*256) +
           (in_data[PW_Start_Address+359]*256*256) +
           (in_data[PW_Start_Address+360]*256) +
           in_data[PW_Start_Address+361] );

  PW_l += 1470;
  /* not sure about this '+=2' to the size ... */
  /* I have but two exemple filesm so ... exemples please ! */
  PW_l = (((PW_l/4)*4)!=PW_l) ? PW_l + 2 : PW_l;


  if ( PW_i >= 64 )
  {
    if ( (in_data[PW_Start_Address-64]  != 0x00 ) ||
         (in_data[PW_Start_Address-63]  != 0x00 ) ||
         (in_data[PW_Start_Address-62]  != 0x03 ) ||
         (in_data[PW_Start_Address-61]  != 0xF3 ) ||
         (in_data[PW_Start_Address-60]  != 0x00 ) ||
         (in_data[PW_Start_Address-59]  != 0x00 ) ||
         (in_data[PW_Start_Address-58]  != 0x00 ) ||
         (in_data[PW_Start_Address-57]  != 0x00 ) ||
         (in_data[PW_Start_Address-56]  != 0x00 ) ||
         (in_data[PW_Start_Address-55]  != 0x00 ) ||
         (in_data[PW_Start_Address-54]  != 0x00 ) ||
         (in_data[PW_Start_Address-53]  != 0x01 ) ||
         (in_data[PW_Start_Address-52]  != 0x00 ) ||
         (in_data[PW_Start_Address-51]  != 0x00 ) ||
         (in_data[PW_Start_Address-50]  != 0x00 ) ||
         (in_data[PW_Start_Address-49]  != 0x00 ) )
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




void Rip_TryIt101 ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  if ( Amiga_EXE_Header == BAD )
  {
    PW_Start_Address -= 32;
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
    Save_Rip_Special ( "Try-It Cruncher 1.01 Exe-file", TryIt, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 64;
    Save_Rip ( "Try-It Cruncher 1.01 Exe-file", TryIt );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 65);  /* 64 should do but call it "just to be sure" :) */
}
