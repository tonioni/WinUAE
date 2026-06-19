/*
 thanks to Laurent Clevy
 20091125 - Asle
 20160319 - reworked from scratch
*/
/* testSIDMON2() */
/* Rip_SIDMON2() */



#include "globals.h"
#include "extern.h"


int16_t	 testSIDMON2 ( void )
{
  if (PW_i<58)
    return BAD;

  
  PW_Start_Address = PW_i-58;
  if ((PW_Start_Address + 0x5a) > PW_in_size)
    return BAD;

  if ( (in_data[PW_Start_Address+6] != 0x00) ||
       (in_data[PW_Start_Address+7] != 0x00) ||
       (in_data[PW_Start_Address+8] != 0x00) ||
       (in_data[PW_Start_Address+9] != 0x1c) ||
       (in_data[PW_Start_Address+10]!= 0x00) ||
       (in_data[PW_Start_Address+11] != 0x00) ||
       (in_data[PW_Start_Address+12] != 0x00) ||
       (in_data[PW_Start_Address+13] != 0x04) ||
       (in_data[PW_Start_Address+68] != '-') ||
       (in_data[PW_Start_Address+69] != ' ') ||
       (in_data[PW_Start_Address+70] != 'T'))
     return BAD;

  /* successives sizes */
  PW_n = 0;
  for (PW_j = 0; PW_j<11; PW_j++)
  {
    PW_m = (( in_data[PW_Start_Address+14+(PW_j*4)]*256*256*256)+
            ( in_data[PW_Start_Address+15+(PW_j*4)]*256*256)+
            ( in_data[PW_Start_Address+16+(PW_j*4)]*256)+
              in_data[PW_Start_Address+17+(PW_j*4)] );
    PW_n += PW_m;
    if ( ((PW_Start_Address + PW_n) > PW_in_size ) || (PW_m > 0xffff) || (PW_m == 0))
    {
      /*printf ( "#1,%ld (start:%ld) (size:%ld)\n" , PW_j, PW_Start_Address , PW_m);*/
      return BAD;
    }
    if (PW_j == 7)
    {
      PW_k = (( in_data[PW_Start_Address+4]*256)+
                in_data[PW_Start_Address+5] );
      if (PW_k != PW_m)
      {
        /*printf ( "#1,a (start:%ld) (size:%ld) (PW_k:%ld)\n" , PW_Start_Address , PW_m,PW_k);*/
        return BAD;
      }
    }
  }
  /*printf ("(PW_n:%u)\n",PW_n);*/


  return GOOD;
}



void Rip_SIDMON2 ( void )
{

  OutputSize = PW_i - PW_Start_Address;

  CONVERT = BAD;
  Save_Rip ( "Sidmon v2 module", Sidmon2 );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}

