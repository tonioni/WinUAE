/* testIFF() */
/* Rip_IFF() */

#include "globals.h"
#include "extern.h"


int16_t	 testIFF ( void )
{
  PW_Start_Address = PW_i;

  if ( PW_Start_Address + 20 > PW_in_size )
  {
/*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

/* ILBM -> picture */
  if ( (in_data[PW_Start_Address+8] != 'I' ) ||
       (in_data[PW_Start_Address+9] != 'L' ) ||
       (in_data[PW_Start_Address+10] != 'B' ) ||
       (in_data[PW_Start_Address+11] != 'M' ))
  {
/*printf ( "#2 Start:%d\n" , PW_Start_Address );*/
    return BAD;
  }

  PW_m = 16;
  while (1)
  {
    /* size of hunk */
    PW_l = ( (in_data[PW_Start_Address+PW_m]*256*256*256) +
             (in_data[PW_Start_Address+PW_m+1]*256*256) +
             (in_data[PW_Start_Address+PW_m+2]*256) +
             in_data[PW_Start_Address+PW_m+3] );
    if (((PW_l/2)*2) != PW_l) PW_l += 1;
/*printf ("at %x (%x) - ",(PW_Start_Address+PW_m),PW_l);*/

    PW_m += 4;
    PW_m += PW_l;
    
    if ((PW_Start_Address+PW_m + 8 > PW_in_size)||(PW_l == 0))
    {
/*  printf ( "#3 Start:%d\n" , PW_Start_Address );*/
      return BAD;
    }
    
    if ((in_data[PW_Start_Address+PW_m] == 'B') &&
        (in_data[PW_Start_Address+PW_m+1] == 'O') &&
        (in_data[PW_Start_Address+PW_m+2] == 'D') &&
        (in_data[PW_Start_Address+PW_m+3] == 'Y'))
      break;
/*printf ("%02x %02x %02x %02x\n",in_data[PW_Start_Address+PW_m],in_data[PW_Start_Address+PW_m+1],in_data[PW_Start_Address+PW_m+2],in_data[PW_Start_Address+PW_m+3]);*/

    PW_m += 4;
    
  }

  PW_k = ( (in_data[PW_Start_Address+PW_m+4]*256*256*256) +
           (in_data[PW_Start_Address+PW_m+5]*256*256) +
           (in_data[PW_Start_Address+PW_m+6]*256) +
           in_data[PW_Start_Address+PW_m+7] );
  PW_l = PW_m + 8 + PW_k;
/*printf ("\nPW_l %d\n",PW_l);
printf ("PW_m %d\n",PW_m);
printf ("PW_Start_Address %d\n",PW_Start_Address);
printf ("PW_k %d\n",PW_k);*/

  return GOOD;
  /* PW_l is the size of the picture */
}

void Rip_IFF ( void )
{
  /* PW_l is still the whole size */

  OutputSize = PW_l;

  CONVERT = BAD;

  Save_Rip ( "IFF graphic", IFF );
  
  if ( Save_Status == GOOD )
    PW_i += 4;
}
