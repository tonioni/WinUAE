/* testHQCCruncher2() */
/* Rip_HQCCruncher2() */

#include "globals.h"
#include "extern.h"


short testHQCCruncher2 ( void )
{
  PW_Start_Address = PW_i - 64;

  if ( (in_data[PW_Start_Address+1776] != 0x02 ) ||
       (in_data[PW_Start_Address+1777] != 0x4D ) ||
       (in_data[PW_Start_Address+1778] != 0x45 ) ||
       (in_data[PW_Start_Address+1779] != 0x58 ) ||
       (in_data[PW_Start_Address+1780] != 0x00 ) ||
       (in_data[PW_Start_Address+1781] != 0x00 ) ||
       (in_data[PW_Start_Address+1782] != 0x03 ) ||
       (in_data[PW_Start_Address+1783] != 0xEC ) ||
       (in_data[PW_Start_Address+1784] != 0x00 ) ||
       (in_data[PW_Start_Address+1785] != 0x00 ) ||
       (in_data[PW_Start_Address+1786] != 0x00 ) ||
       (in_data[PW_Start_Address+1787] != 0x00 ) ||
       (in_data[PW_Start_Address+1788] != 0x00 ) ||
       (in_data[PW_Start_Address+1789] != 0x00 ) ||
       (in_data[PW_Start_Address+1790] != 0x03 ) ||
       (in_data[PW_Start_Address+1791] != 0xF2 ) )
  {
    /* should be enough :))) */
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
    
  }


  /* packed size */
  PW_l = ( (in_data[PW_Start_Address+1808]*256*256*256) +
           (in_data[PW_Start_Address+1809]*256*256) +
           (in_data[PW_Start_Address+1810]*256) +
           in_data[PW_Start_Address+1811] );

  PW_l *= 4;
  PW_l += 1816;


/*  if ( PW_i >= 50 ) */
/*  { */
/*    if ( (in_data[PW_Start_Address-50]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-49]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-48]  != 0x03 ) || */
/*         (in_data[PW_Start_Address-47]  != 0xF3 ) || */
/*         (in_data[PW_Start_Address-46]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-45]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-44]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-43]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-42]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-41]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-40]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-39]  != 0x02 ) || */
/*         (in_data[PW_Start_Address-38]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-37]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-36]  != 0x00 ) || */
/*         (in_data[PW_Start_Address-35]  != 0x00 ) ) */
/*    { */
/*      Amiga_EXE_Header = BAD; */
/*    } */
/*    else */
      Amiga_EXE_Header = GOOD;
/*  } */
/*  else */
/*    Amiga_EXE_Header = BAD; */

  return GOOD;
  /* PW_l is the size of the pack */
}



void Rip_HQCCruncher2 ( void )
{
  /* PW_l is still the whole size */

  Uchar * Amiga_EXE_Header_Block;
  Uchar * Whatever;

  OutputSize = PW_l;

  CONVERT = BAD;

  /*---------------------------------------------------*/
  /* not used yet coz I still dont know hoz to rebuild */
  /* 3 chunks EXE cases ...                            */
  /* so, the code below IS useless AND wrong !!!!!     */
  /*---------------------------------------------------*/
  if ( Amiga_EXE_Header == BAD )
  {
    OutputSize -= 50;
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
    PW_j = PW_l - 60;
    PW_j /= 4;
    Whatever = (Uchar *) &PW_j;
    Amiga_EXE_Header_Block[20] = Amiga_EXE_Header_Block[32] = *(Whatever+3);
    Amiga_EXE_Header_Block[21] = Amiga_EXE_Header_Block[33] = *(Whatever+2);
    Amiga_EXE_Header_Block[22] = Amiga_EXE_Header_Block[34] = *(Whatever+1);
    Amiga_EXE_Header_Block[23] = Amiga_EXE_Header_Block[35] = *Whatever;

    /* also the last 24 bytes are 'removed' frequently ... Here they are */
    in_data[PW_Start_Address+OutputSize-24] = 0x00;
    in_data[PW_Start_Address+OutputSize-23] = 0x00;
    in_data[PW_Start_Address+OutputSize-22] = 0x03;
    in_data[PW_Start_Address+OutputSize-21] = 0xEC;

    in_data[PW_Start_Address+OutputSize-20] = 0x00;
    in_data[PW_Start_Address+OutputSize-19] = 0x00;
    in_data[PW_Start_Address+OutputSize-18] = 0x00;
    in_data[PW_Start_Address+OutputSize-17] = 0x00;

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

    Save_Rip_Special ( "HQC Cruncher 2.0 Exe-file", HQC, Amiga_EXE_Header_Block , 50 );
    free ( Amiga_EXE_Header_Block );
  }
  else
  {
    Save_Rip ( "HQC Cruncher 2.0 Exe-file", HQC );
  }
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 65);  /* 64 should do but call it "just to be sure" :) */
}

