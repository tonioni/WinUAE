#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testFUZZAC ( void )
{
  PW_Start_Address = PW_i;

  /* test finetune */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+72+PW_k*68] > 0x0F )
    {
      return BAD;
    }
  }

  /* test volumes */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+73+PW_k*68] > 0x40 )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    PW_j = (in_data[PW_Start_Address+66+PW_k*68]*256)+in_data[PW_Start_Address+67+PW_k*68];
    if ( PW_j > 0x8000 )
    {
      return BAD;
    }
    PW_WholeSampleSize += (PW_j * 2);
  }

  /* test size of pattern list */
  if ( in_data[PW_Start_Address+2114] == 0x00 )
  {
    return BAD;
  }

  return GOOD;
}

