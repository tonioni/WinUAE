#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testPM40 ( void )
{
  PW_Start_Address = PW_i;

  /* size of the pattern list */
  PW_j = in_data[PW_Start_Address+7];
  if ( PW_j > 0x7f )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* PW_j is the size of the pattern list */

  /* finetune */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+PW_k*8+266] > 0x0f )
    {
/*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  
  /* volume */
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+PW_k*8+267] > 0x40 )
    {
/*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
      return BAD;
    }
  }
  
  /* sample data address */
  PW_l = ( (in_data[PW_Start_Address+512]*256*256*256)+
	   (in_data[PW_Start_Address+513]*256*256)+
	   (in_data[PW_Start_Address+514]*256)+
	   in_data[PW_Start_Address+515] );
  if ( (PW_l <= 520) || (PW_l > 2500000l ) )
  {
/*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  /* PW_l is the sample data address */
  return GOOD;
}

