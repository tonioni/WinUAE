/* testBP() */
/* Rip_BP() */

#include "globals.h"
#include "extern.h"


short testBP ( void )
{
  /* test 1 */
  if ( (PW_i < 26) || ((PW_Start_Address+512)>PW_in_size) )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-26;
  for ( PW_j=0 ; PW_j<15 ; PW_j++ )
  {
    if ( in_data[32+PW_j*32+PW_Start_Address] == 0xff )
      continue;
    if ( in_data[PW_j*32+63+PW_Start_Address] > 0x40 )
      return BAD;
  }

  /* various shits to calculate the size */
  PW_WholeSampleSize = 0;
  for ( PW_k=0 ; PW_k<15 ; PW_k++ )
  {
    if ( in_data[PW_Start_Address+32+32*PW_k] != 0xff )
      PW_WholeSampleSize += (((in_data[PW_Start_Address+56+32*PW_k]*256)+in_data[PW_Start_Address+57+32*PW_k])*2);
  }
  PW_j = in_data[PW_Start_Address+29];
  PW_l = in_data[PW_Start_Address+30]*256 + in_data[PW_Start_Address+31];
  OutputSize = PW_WholeSampleSize + (PW_j*64);
  PW_j = 0;
  if ( (PW_Start_Address+525+(PW_l*16)) > PW_in_size )
    return BAD;

  for ( PW_k=0 ; PW_k<PW_l ; PW_k++ )
  {
    if ( (in_data[PW_Start_Address+512+PW_k*16]*256 +
	  in_data[PW_Start_Address+513+PW_k*16]) > PW_j )
      PW_j = (in_data[PW_Start_Address+512+PW_k*16]*256 + in_data[PW_Start_Address+513+PW_k*16]);
    if ( (in_data[PW_Start_Address+516+PW_k*16]*256 +
	  in_data[PW_Start_Address+517+PW_k*16]) > PW_j )
      PW_j = (in_data[PW_Start_Address+516+PW_k*16]*256 + in_data[PW_Start_Address+517+PW_k*16]);
    if ( (in_data[PW_Start_Address+520+PW_k*16]*256 +
	  in_data[PW_Start_Address+521+PW_k*16]) > PW_j )
      PW_j = (in_data[PW_Start_Address+520+PW_k*16]*256 + in_data[PW_Start_Address+521+PW_k*16]);
    if ( (in_data[PW_Start_Address+524+PW_k*16]*256 +
	  in_data[PW_Start_Address+525+PW_k*16]) > PW_j )
      PW_j = (in_data[PW_Start_Address+524+PW_k*16]*256 + in_data[PW_Start_Address+525+PW_k*16]);
  }

  return GOOD;
}


/* Rip_BP */
void Rip_BP ( void )
{
  OutputSize += ((PW_j*48) + (PW_l*16) + 512);

  CONVERT = BAD;
  Save_Rip ( "Sound Monitor v2 / v3 module", SoundMonitor );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 27);  /* 26 should do but call it "just to be sure" :) */
}

