/*
 * THX - AHX ripper
 * 20130413 - slightly adapted to HVL ripping
*/
/* testTHX() */
/* Rip_THX() */



#include "globals.h"
#include "extern.h"


int16_t	 testTHX ( void )
{
  PW_Start_Address = PW_i;

  /* file size < 113 */
  if ( (PW_in_size - PW_Start_Address) < 30 )
  {
    /*printf ( "#1 (start:%ld) (size:%ld)\n" , PW_Start_Address , PW_in_size-PW_Start_Address);*/
    return BAD;
  }

  /* get nbr byte to bypass to reach txts */
  PW_m = ( ( in_data[PW_Start_Address+4]*256) + in_data[PW_Start_Address+5] );

  /* test in-size again */
  if ( PW_Start_Address+PW_m > PW_in_size )
  {
/*    printf ( "#2 (start:%d) (1st smp addy:%d)\n" , PW_Start_Address , PW_m);*/
    return BAD;
  }

  /* test patternlist size */
  PW_j = (in_data[PW_Start_Address+6] & 0x0f)*256 + in_data[PW_Start_Address+7];
  /* only 999 position are possible - but I've seen 1024 ... */
  if ( PW_j > 1024 )
  {
/*    printf ( "#3 (start:%d) (1st smp addy:%d)\n" , PW_Start_Address , PW_m);*/
    return BAD;
  }

  /* test patternlist restart - AHX only */
  if (in_data[PW_Start_Address] == 'T')
  {
    PW_k = (in_data[PW_Start_Address+8] & 0x0f)*256 + in_data[PW_Start_Address+9];
    /* only 999 position are possible - but I've seen 1024 ... */
    if ( PW_k >= PW_j )
    {
      /*printf ( "#4 (start:%d) (1st smp addy:%d)\n" , PW_Start_Address , PW_m);*/
      return BAD;
    }
  }

  PW_k = in_data[PW_Start_Address+10];
  /* track size */
  if ( PW_k > 64 )
  {
    /*printf ( "#5 (start:%d) (track size:%d)\n" , PW_Start_Address , PW_k);*/
    return BAD;
  }

  return GOOD;
}



void Rip_THX ( void )
{
  /* PW_m is the address of txt */

  uint32_t	 Where = PW_Start_Address+PW_m;
  uint8_t SMP = 0x00,a = 0x00;
  SMP = in_data[PW_Start_Address+12];

  OutputSize = PW_m;

  /* title */
  if (in_data[Where] == 0x00)
    OutputSize += 1;
  else
    while (in_data[Where] != 0x00)
      Where += 1;

  Where += 1;
  if (SMP != 0x00)
  {
    while (a<SMP)
    {
      if ( Where > PW_in_size )
        break;
      if (in_data[Where] == 0x00)
        a+=0x01;
      Where += 1;
    }
  }

  OutputSize = Where - PW_Start_Address;

  CONVERT = BAD;

  if (in_data[PW_Start_Address] == 'T')
    Save_Rip ( "AHX v1/v2 module", AHX );
  if (in_data[PW_Start_Address] == 'H')
    Save_Rip ( "Hively Tracker module", HVL );
  
  
  if ( Save_Status == GOOD )
    PW_i += 1; /* after THX/HVL tag */
}

