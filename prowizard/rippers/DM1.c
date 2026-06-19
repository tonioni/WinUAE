/*
  Delta Music 1 ("shot in the dark" try)
  20160309 - Asle
*/
/* testDM1() */
/* Rip_DM1() */



#include "globals.h"
#include "extern.h"


int16_t testDM1 ( void )
{
  int32_t t[64],a;
  PW_Start_Address = PW_i;

  /* file size */
  if ( (PW_Start_Address + 104) > PW_in_size )
  {
    /*printf ( "#1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address);*/
    return BAD;
  }

  /* get track sizes */
  for ( a=0; a<4; a++)
  {
    t[a] = (( in_data[PW_Start_Address+6+(a*4)]*256)+
              in_data[PW_Start_Address+7+(a*4)] );
    if (t[a] > 0x7fff) return BAD;
  }

  /* get note block size */
  t[4] = (( in_data[PW_Start_Address+22]*256) + in_data[PW_Start_Address+23] );


  /* get sounds sizes */
  for ( a=0; a<20; a++)
  {
    t[a+40] = (( in_data[PW_Start_Address+25+(a*4)]*256*256)+
               ( in_data[PW_Start_Address+26+(a*4)]*256)+
                 in_data[PW_Start_Address+27+(a*4)] );
    if (t[a+40] > 0xffff) return BAD;
  }

  /* file size */
  PW_l = t[0] + t[1] + t[2] + t[3] + t[4] + 104;
  if ( (PW_Start_Address + PW_l) > PW_in_size)
  {
    printf ( "#1,3 (start:%d) (t[0]:%x)(t[1]:%x)(t[2]:%x)(t[3]:%x)(t[4]:%x)\n" , PW_Start_Address , t[0],t[1],t[2],t[3],t[4]);
    return BAD;
  }

  for ( a=40; a<60; a++)PW_l += t[a];

  return GOOD;
}



void Rip_DM1 ( void )
{
  /* PW_l contains the whole size */
  OutputSize = PW_l;

  CONVERT = BAD;
  Save_Rip ( "Delta Music 1", DM1 );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}

