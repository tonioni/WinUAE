#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Rip_JamCracker ( void )
{
  /* PW_j is the number of sample */
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_l is the number of pattern saved */
  /* PW_m is the size of the pattern list */

  /* first, get rid of header size (til end of pattern list) */
  PW_n = 6 + (PW_j*40) + 2;
  OutputSize = PW_n + (PW_l*6) + 2 + (PW_m*2);

  /* PW_n points now at the beginning of pattern descriptions */
  /* now, let's calculate pattern data size */
  /* first address : */
  PW_o =((in_data[PW_Start_Address+PW_n+2]*256*256*256)+
         (in_data[PW_Start_Address+PW_n+3]*256*256)+
         (in_data[PW_Start_Address+PW_n+4]*256)+
          in_data[PW_Start_Address+PW_n+5] );
  PW_n += (PW_l*6);
  PW_k =((in_data[PW_Start_Address+PW_n-4]*256*256*256)+
         (in_data[PW_Start_Address+PW_n-3]*256*256)+
         (in_data[PW_Start_Address+PW_n-2]*256)+
          in_data[PW_Start_Address+PW_n-1] );
  PW_k -= PW_o;
  /* PW_k shoulb be the track data size by now ... save the last pattern ! */
  /* let's get its number of lines */
  PW_o = in_data[PW_Start_Address+PW_n-5];
  PW_o *= 4;  /* 4 voices */
  PW_o *= 8;  /* 8 bytes per note */

  OutputSize += PW_WholeSampleSize + PW_k + PW_o;

  /*  printf ( "\b\b\b\b\b\b\b\bJamCracker / Pro module found at %ld !. its size is : %ld\n" , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[JamCracker][0];
  OutName[2] = Extensions[JamCracker][1];
  OutName[3] = Extensions[JamCracker][2];*/

  CONVERT = BAD;
  Save_Rip ( "JamCracker / Pro module", JamCracker );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
}

