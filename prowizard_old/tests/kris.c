/*  (3rd of april 2000)
 *    bugs pointed out by Thomas Neumann .. thx :) 
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testKRIS ( void )
{
  /* test 1 */
  if ( (PW_i<952) || ((PW_Start_Address+977)>PW_in_size) )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-952;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    /* volume > 64 ? */
    if ( in_data[PW_Start_Address+47+PW_j*30] > 0x40 )
    {
      return BAD;
    }
    /* finetune > 15 ? */
    if ( in_data[PW_Start_Address+46+PW_j*30] > 0x0f )
    {
      return BAD;
    }
  }

  return GOOD;
}

