#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPRUN2 ( void )
{
  PW_Start_Address = PW_i;
  PW_j = (in_data[PW_i+4]*256*256*256)+(in_data[PW_i+5]*256*256)+(in_data[PW_i+6]*256)+in_data[PW_i+7];

  /* test sample_data address */
  if ( (PW_j+PW_Start_Address) > PW_in_size )
  {
    return BAD;
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+11+PW_k*8] > 0x40 )
    {
      return BAD;
    }
  }

  /* test finetunes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+10+PW_k*8] > 0x0F )
    {
      return BAD;
    }
  }

  return GOOD;
}

