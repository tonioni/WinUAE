#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_GnuPlayer ( void )
{
  /*PW_WholeSampleSize is the whole sample size */
  /*but it seems to be a fake sample values */
  
  /* size of 1st track + header */
  PW_k = (in_data[PW_Start_Address + 0x96]*256)+in_data[PW_Start_Address + 0x97] + 0x96;
  /* size of 2nd track */
  PW_j = (in_data[PW_k]*256) + in_data[PW_k+1] + PW_k;
  /* PW_j points now to the first sample size */
  /*printf ( "\nWhere before 1st sample : %ld\n" , PW_j);*/

  /* real sample sizes */
  PW_m = 0;
  while ( PW_m < PW_o )
  {
    PW_k = (in_data[PW_Start_Address+PW_j]*256) + in_data[PW_Start_Address+PW_j+1];
    PW_j += (PW_k + 2);
    PW_m += 1;
    /*printf ( "sample %ld : siz:%ld where:%ld\n", PW_m,PW_k,PW_j );
      fflush (stdout);*/
  }

  OutputSize = PW_j;

  CONVERT = GOOD;
  Save_Rip ( "GnuPlayer module", GnuPlayer );
  
  if ( Save_Status == GOOD )
    PW_i += 0x96;
}

