/* update on the 3rd of april 2000 */
/* bug pointes out by Thomas Neumann ... thx */

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

short testSTIM ( void )
{
  PW_Start_Address = PW_i;

  /*  */
  PW_j = ((in_data[PW_Start_Address+4]*256*256*256)+
	  (in_data[PW_Start_Address+5]*256*256)+
	  (in_data[PW_Start_Address+6]*256)+
	  in_data[PW_Start_Address+7]);
  if ( PW_j < 406 )
  {
    return BAD;
  }

  /* size of the pattern list */
  PW_k = ((in_data[PW_Start_Address+18]*256)+
	  in_data[PW_Start_Address+19]);
  if ( PW_k > 128 )
  {
    return BAD;
  }

  /* nbr of pattern saved */
  PW_k = ((in_data[PW_Start_Address+20]*256)+
	  in_data[PW_Start_Address+21]);
  if ( (PW_k > 64) || (PW_k == 0) )
  {
    return BAD;
  }

  /* pattern list */
  for ( PW_l=0 ; PW_l<128 ; PW_l++ )
  {
    if ( in_data[PW_Start_Address+22+PW_l] > PW_k )
    {
      return BAD;
    }
  }

  /* test sample sizes */
  PW_WholeSampleSize = 0;
  for ( PW_l=0 ; PW_l<31 ; PW_l++ )
  {
    /* addresse de la table */
    PW_o = PW_Start_Address+PW_j+PW_l*4;

    /* address du sample */
    PW_k = ((in_data[PW_o]*256*256*256)+
	    (in_data[PW_o+1]*256*256)+
	    (in_data[PW_o+2]*256)+
	    in_data[PW_o+3]);

    /* taille du smp */
    PW_m = ((in_data[PW_o+PW_k-PW_l*4]*256)+
	    in_data[PW_o+PW_k+1-PW_l*4])*2;

    PW_WholeSampleSize += PW_m;
  }

  if ( PW_WholeSampleSize <= 4 )
  {
    return BAD;
  }

  /* PW_WholeSampleSize is the size of the sample data */
  /* PW_j is the address of the sample desc */
  return GOOD;
}
