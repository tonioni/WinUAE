/* testDefjam32()    */
/* testDefjam32pro() */
/* testDefjam32t() */
/* Rip_Defjam32()    */

#include "globals.h"
#include "extern.h"


short testDefjam32 ( void )
{
  PW_Start_Address = PW_i-2;

  if ( (in_data[PW_Start_Address+12] != 0x28 ) ||
       (in_data[PW_Start_Address+13] != 0x7A ) ||
       (in_data[PW_Start_Address+14] != 0x01 ) ||
       (in_data[PW_Start_Address+15] != 0x52 ) ||
       (in_data[PW_Start_Address+16] != 0x20 ) ||
       (in_data[PW_Start_Address+17] != 0x4C ) ||
       (in_data[PW_Start_Address+18] != 0xD1 ) ||
       (in_data[PW_Start_Address+19] != 0xFC ) ||
       (in_data[PW_Start_Address+24] != 0xB3 ) ||
       (in_data[PW_Start_Address+25] != 0xCC ) ||
       (in_data[PW_Start_Address+26] != 0x6E ) ||
       (in_data[PW_Start_Address+27] != 0x08 ) ||
       (in_data[PW_Start_Address+28] != 0x20 ) ||
       (in_data[PW_Start_Address+29] != 0x49 ) ||
       (in_data[PW_Start_Address+30] != 0xD1 ) ||
       (in_data[PW_Start_Address+31] != 0xFA ) ||
       (in_data[PW_Start_Address+32] != 0xFF ) ||
       (in_data[PW_Start_Address+33] != 0xF4 ) ||
       (in_data[PW_Start_Address+34] != 0x60 ) ||
       (in_data[PW_Start_Address+35] != 0x06 ) ||
       (in_data[PW_Start_Address+36] != 0x18 ) ||
       (in_data[PW_Start_Address+37] != 0xD9 ) ||
       (in_data[PW_Start_Address+38] != 0xB9 ) ||
       (in_data[PW_Start_Address+39] != 0xC8 ) ||
       (in_data[PW_Start_Address+40] != 0x6D ) ||
       (in_data[PW_Start_Address+41] != 0xFA ) ||
       (in_data[PW_Start_Address+42] != 0x43 ) ||
       (in_data[PW_Start_Address+43] != 0xF9 ) ||
       (in_data[PW_Start_Address+44] != 0x00 ) )
  {
    /* should be enough :))) */
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+20]*256*256*256) +
           (in_data[PW_Start_Address+21]*256*256) +
           (in_data[PW_Start_Address+22]*256) +
           in_data[PW_Start_Address+23] );

  PW_l += 692;

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


short testDefjam32pro ( void )
{

  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+16] != 0x3D ) ||
       (in_data[PW_Start_Address+17] != 0x40 ) ||
       (in_data[PW_Start_Address+18] != 0x00 ) ||
       (in_data[PW_Start_Address+19] != 0x9A ) ||
       (in_data[PW_Start_Address+20] != 0x3D ) ||
       (in_data[PW_Start_Address+21] != 0x40 ) ||
       (in_data[PW_Start_Address+22] != 0x00 ) ||
       (in_data[PW_Start_Address+23] != 0x9C ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+180]*256*256*256) +
           (in_data[PW_Start_Address+181]*256*256) +
           (in_data[PW_Start_Address+182]*256) +
           in_data[PW_Start_Address+183] );

  PW_l += 796;

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


short testDefjam32t ( void )
{

  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+12] != 0x28 ) ||
       (in_data[PW_Start_Address+13] != 0x7A ) ||
       (in_data[PW_Start_Address+14] != 0x01 ) ||
       (in_data[PW_Start_Address+15] != 0x54 ) ||
       (in_data[PW_Start_Address+16] != 0x20 ) ||
       (in_data[PW_Start_Address+17] != 0x4C ) ||
       (in_data[PW_Start_Address+18] != 0xD1 ) ||
       (in_data[PW_Start_Address+19] != 0xFC ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+20]*256*256*256) +
           (in_data[PW_Start_Address+21]*256*256) +
           (in_data[PW_Start_Address+22]*256) +
           in_data[PW_Start_Address+23] );

  PW_l += 692;

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


void Rip_Defjam32 ( void )
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

    /* also the last 4 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-4]  = 0x00;
    in_data[PW_Start_Address+OutputSize-3]  = 0x00;
    in_data[PW_Start_Address+OutputSize-2]  = 0x03;
    in_data[PW_Start_Address+OutputSize-1]  = 0xF2;

    Save_Rip_Special ( "Defjam Cruncher 3.2 / pro / t Exe-file", Defjam_32, Amiga_EXE_Header_Block , 32 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    PW_Start_Address -= 32;
    Save_Rip ( "Defjam Cruncher 3.2 / pro / t Exe-file", Defjam_32 );
  }

  if ( Save_Status == GOOD )
    PW_i += 36;
}
