/* testBSIFutureComposer() */
/* Rip_BSIFutureComposer() */

#include "globals.h"
#include "extern.h"

short testBSIFutureComposer ( void )
{
  PW_Start_Address = PW_i;

  /* file size < 18424 */
  if ( test_1_start(PW_Start_Address + 18424) )
  {
/*printf ( "#1 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
    return BAD;
  }

  if (( in_data[PW_Start_Address+17412] != 'D' ) ||
      ( in_data[PW_Start_Address+17413] != 'I' ) ||
      ( in_data[PW_Start_Address+17414] != 'G' ) ||
      ( in_data[PW_Start_Address+17415] != 'I' ) )
  {
/*printf ( "#2 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
     return BAD;
  }

  if (( in_data[PW_Start_Address+18424] != 'D' ) ||
      ( in_data[PW_Start_Address+18425] != 'I' ) ||
      ( in_data[PW_Start_Address+18426] != 'G' ) ||
      ( in_data[PW_Start_Address+18427] != 'P' ) )
  {
/*printf ( "#3 (start:%ld) (number of samples:%ld)\n" , PW_Start_Address , PW_j);*/
     return BAD;
  }

  return GOOD;
}


/* Rip_BSIFutureComposer */
void Rip_BSIFutureComposer ( void )
{
  /* get whole sample size */
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<63 ; PW_j++ )
  {
    PW_o =((in_data[PW_Start_Address+17420+(PW_j*16)]*256*256*256)+
           (in_data[PW_Start_Address+17421+(PW_j*16)]*256*256)+
           (in_data[PW_Start_Address+17422+(PW_j*16)]*256)+
            in_data[PW_Start_Address+17423+(PW_j*16)] );
    PW_WholeSampleSize += PW_o;
  }

  OutputSize = PW_WholeSampleSize + 18428;

  CONVERT = BAD;
  Save_Rip ( "BSI Future Composer module", BSIFC );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}
