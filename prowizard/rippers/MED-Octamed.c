/* testMMD0() */
/* Rip_MMD0() */

#include "globals.h"
#include "extern.h"


/* valid for MMD0 & MMD1 */
short testMMD0 ( void )
{
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 52) > PW_in_size )
    return BAD;

  /* get the 'should be' module size */
  PW_k = ((in_data[PW_Start_Address+4]*256*256*256)+
          (in_data[PW_Start_Address+5]*256*256)+
          (in_data[PW_Start_Address+6]*256)+
           in_data[PW_Start_Address+7] );

  /* 52 : size of header */
  if ( PW_k < 52 )
  {
/*printf ( "#1 (start:%ld)\n" , PW_Start_Address );*/
    return BAD;
  }

  /* test flag byte : 1 or 0 */
  PW_j = in_data[PW_Start_Address+20];
  if ( PW_j > 1 )
  {
/*printf ( "#2 (start:%ld) (flag:%ld)\n" , PW_Start_Address,PW_j );*/
    return BAD;
  }

  /* get struct MMD* addy */
  PW_j = ((in_data[PW_Start_Address+8] *256*256*256)+
          (in_data[PW_Start_Address+9] *256*256)+
          (in_data[PW_Start_Address+10]*256)+
           in_data[PW_Start_Address+11] );
  if ( (PW_j < 52) || (PW_j > PW_k) )
  {
/*printf ( "#3 (start:%ld) (siz:%ld) (struct addy:%ld)\n" , PW_Start_Address,PW_k,PW_j );*/
    return BAD;
  }

  /* test 'reserved' bytes which should be set to 0x00 */
  if ( (in_data[PW_Start_Address+21] != 0x00) ||
       (in_data[PW_Start_Address+22] != 0x00) ||
       (in_data[PW_Start_Address+23] != 0x00))
  {
/*printf ( "#4 (start:%ld) (21:%x) (22:%x) (23:%x)\n"
          , PW_Start_Address
          , in_data[PW_Start_Address+21]
          , in_data[PW_Start_Address+22]
          , in_data[PW_Start_Address+23]);*/
    return BAD;
  }

  /* stop it for now ... few/cheap tests here, I agree .. */
  /* PW_k is the module size */

  return GOOD;
}


void Rip_MMD0 ( void )
{
  /* PW_k is the module size */

  OutputSize = PW_k;

  CONVERT = BAD;
  if ( in_data[PW_i+3] == '0' )
    Save_Rip ( "MED (MMD0) module", MED );
  if ( in_data[PW_i+3] == '1' )
    Save_Rip ( "OctaMED (MMD1) module", MED );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}

