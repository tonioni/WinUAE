#include "globals.h"
#include "extern.h"


/*
 * group of tests funcs that are common to most of test\*.c files
*/

/* start : if mod can possibly fit from the beginning of the file */
/* e.g. M.K. before 1080th byte */
short test_1_start ( Ulong LIMIT )
{
  return ( PW_i < LIMIT) ? BAD : GOOD;
}

short test_smps ( long smpsiz, long lstart, long lsiz, Uchar vol, Uchar fine )
{
  if ( lstart > smpsiz )
    return BAD;
  if ( lsiz > (smpsiz + 2) )
    return BAD;
  if ( (lstart + lsiz) > smpsiz+2 )
    return BAD;
  if ( (lstart != 0) && ( lsiz <= 2 ) )
    return BAD;
  if ( ((lstart != 0) || (lsiz > 2)) && (smpsiz = 0) )
    return BAD;
  if ( (vol > 0x40) || (fine > 0x0f) )
    return BAD;
  return GOOD;
}
