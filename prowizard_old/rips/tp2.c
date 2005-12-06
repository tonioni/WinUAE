#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_TP2 ( void )
{
  /* PW_j is the size of the pattern list */
  /* PW_l is the number of sample */
  /* PW_WholeSampleSize is the sample data size */


  PW_m=0;
  for ( PW_k=0 ; PW_k<PW_j ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_l*8+32+PW_k*2]*256)+in_data[PW_Start_Address+PW_l*8+33+PW_k*2];
    if ( PW_o > PW_m )
      PW_m = PW_o;
  }
  /* PW_m is the highest pattern number */
  PW_m += 8;
  /* PW_m is now the size of the track table list */
  PW_n = 0;
/*printf ( "highest pattern : %ld (%x)\n" , PW_m , PW_m );*/
  for ( PW_k=0 ; PW_k<(PW_m/2) ; PW_k++ )
  {
    PW_o = (in_data[PW_Start_Address+PW_l*8+32+PW_j*2+PW_k*2]*256)+in_data[PW_Start_Address+PW_l*8+33+PW_j*2+PW_k*2];
/*printf ( "%4x, " , PW_o );*/
    if ( PW_o > PW_n )
      PW_n = PW_o;
  }
/*printf ( "\nhighest : %ld (%x)\n" , PW_n , PW_n );*/
/*printf ( "track data address : %ld (%x)\n" , (34+8*PW_l+2*PW_j+PW_m ),(34+8*PW_l+2*PW_j+PW_m));*/
  PW_n += (34+8*PW_l+2*PW_j+PW_m);
/*printf ( "address of last track : %ld\n" , PW_n );*/
  OutputSize = PW_n;
  

  /* all vars are availlable now, save PW_WholeSampleSize */

  /* now counting size of the last pattern ... pfiew .. */
  PW_l = 0;
  for ( PW_j=0 ; PW_j<64 ; PW_j++ )
  {
/*printf ( "%ld," , PW_l );*/
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0xC0 ) == 0xC0 )
    {
      PW_j += (0x100-in_data[PW_Start_Address+PW_n+PW_l]);
      PW_j -= 1;
      PW_l += 1;
      continue;
    }
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0xC0 ) == 0x80 )
    {
      PW_l += 2;
      continue;
    }
    PW_l += 1;
    if ( (in_data[PW_Start_Address+PW_n+PW_l]&0x0F ) == 0x00 )
    {
      PW_l += 1;
      continue;
    }
    PW_l += 2;
  }
/*printf ( "\nsize of the last track : %ld\n" , PW_l );*/

  OutputSize += PW_WholeSampleSize + PW_l;
  /*  printf ( "\b\b\b\b\b\b\b\bTracker Packer v2 module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[TP2][0];
  OutName[2] = Extensions[TP2][1];
  OutName[3] = Extensions[TP2][2];*/

  CONVERT = GOOD;
  Save_Rip ( "Tracker Packer v2 module", TP2 );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1); /* 0 could be enough */
}
