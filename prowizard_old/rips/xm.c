#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_XM ( void )
{
  /* PW_l is the header size and points on the first pattern */
  /* PW_j is the number of pattern */
  /* PW_k is the number of instruments */

  /* get whole pattern data siz */
  for ( PW_o=0 ; PW_o<PW_j ; PW_o++ )
  {
    /* getting siz of one pattern */
    PW_m = (in_data[PW_Start_Address+PW_l+8]*256) + in_data[PW_Start_Address+PW_l+7];
    /* adding it to current pointer + 9 being the pat header siz */
    PW_l += (PW_m + 9);
  }

  /* PW_l is now on the first inst .. PW_j is free :)*/
  /* get whole insts data siz */
  for ( PW_o=0 ; PW_o<PW_k ; PW_o++ )
  {
    long siz=0;
    /* getting siz of one inst header */
    PW_m = (in_data[PW_Start_Address+PW_l+1]*256) + in_data[PW_Start_Address+PW_l];
    /* getting nbr of samples in this inst */
    PW_j = (in_data[PW_Start_Address+PW_l+28]*256) + in_data[PW_Start_Address+PW_l+27];
    /* getting sizes of samples */
    PW_l += PW_m; /* so that it points on first sample header */
    for ( PW_n=0 ; PW_n<PW_j ; PW_n++ )
    {
      /* siz of this samples */
      siz += ( (in_data[PW_Start_Address+PW_l+3]*256*256*256) +
	       (in_data[PW_Start_Address+PW_l+2]*256*256) +
	       (in_data[PW_Start_Address+PW_l+1]*256) +
  	       in_data[PW_Start_Address+PW_l]);
      /* move pointer onto the next sample header if one exists*/
      PW_l += 40;
    }
    /* add sample datas of this instrument now */
    PW_l += siz;
  }

  OutputSize = PW_l;

  CONVERT = BAD;
  Save_Rip ( "Fastracker (XM) 2.0 module", XM );
  
  if ( Save_Status == GOOD )
    PW_i += 2;
}
