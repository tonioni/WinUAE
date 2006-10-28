/* testMUGICIAN() */
/* Rip_MUGICIAN() */

#include "globals.h"
#include "extern.h"

short testMUGICIAN ( void )
{
  if ( PW_i<2 )
  {
    /*printf ( "#0.0 (start:%ld)\n" , PW_Start_Address);*/
    return BAD;
  }
  PW_Start_Address = PW_i-2;

  if (( in_data[PW_Start_Address] != ' ' ) ||
      ( in_data[PW_Start_Address+1] != 'M' ) ||
      ( in_data[PW_Start_Address+6] != 'I' ) ||
      ( in_data[PW_Start_Address+7] != 'A' ) ||
      ( in_data[PW_Start_Address+8] != 'N' ) ||
      ( in_data[PW_Start_Address+28] != 0x00 ) ||
      ( in_data[PW_Start_Address+29] != 0x00 ) ||
      ( in_data[PW_Start_Address+32] != 0x00 ) ||
      ( in_data[PW_Start_Address+33] != 0x00 ) ||
      ( in_data[PW_Start_Address+36] != 0x00 ) ||
      ( in_data[PW_Start_Address+37] != 0x00 ) ||
      ( in_data[PW_Start_Address+40] != 0x00 ) ||
      ( in_data[PW_Start_Address+41] != 0x00 ))
  {
    /*printf ( "#0.2 (start:%ld)\n" , PW_Start_Address);*/
     return BAD;
  }

  /* number of (real) samples */
  PW_j = ((in_data[PW_Start_Address + 0x44]*256*256*256)+
	  (in_data[PW_Start_Address + 0x45]*256*256)+
	  (in_data[PW_Start_Address + 0x46]*256)+
	  in_data[PW_Start_Address + 0x47]);
  /* sample data size */
  PW_WholeSampleSize = ((in_data[PW_Start_Address + 0x48]*256*256*256)+
	  (in_data[PW_Start_Address + 0x49]*256*256)+
	  (in_data[PW_Start_Address + 0x4A]*256)+
	  in_data[PW_Start_Address + 0x4B]);
  if (((PW_j != 0) && (PW_WholeSampleSize == 0)) || ((PW_j == 0) && (PW_WholeSampleSize != 0)) || (PW_j>255))
  {
    /*printf ( "#1 (start:%ld) (number of samples:%ld)(samplesize:%ld)\n" , PW_Start_Address , PW_j,PW_WholeSampleSize);*/
    return BAD;
  }

  /* number of instrument */
  PW_k = ((in_data[PW_Start_Address + 0x3C]*256*256*256)+
	  (in_data[PW_Start_Address + 0x3D]*256*256)+
	  (in_data[PW_Start_Address + 0x3E]*256)+
	  in_data[PW_Start_Address + 0x3F]);
  if ( (PW_k == 0) || (PW_k > 255) )
  {
    /*printf ( "#2 (start:%ld) (number of samples:%ld)(nbr inst:%ld)\n" , PW_Start_Address , PW_j,PW_k);*/
    return BAD;
  }


  /*
    not many checks, here, but, to my point of view, it's unlikely
    a music format comes anywhere near this one, so ... .
  */
  /*  printf ("samplesize:%ld,nbrsmp:%ld,nbrinst:%ld\n",PW_WholeSampleSize,PW_j,PW_k);*/
  return GOOD;
}



void Rip_MUGICIAN ( void )
{
  /*
    PW_WholeSampleSize : sample data size
    PW_j : number of sample
    PW_k : number of instrument
  */
  OutputSize = (PW_j * 0x20) + (PW_k * 0x10) + PW_WholeSampleSize + 0x1CC;

  /* nbr of patterns */
  PW_k = ((in_data[PW_Start_Address + 0x1a]*256)+
	  in_data[PW_Start_Address + 0x1b]);
  OutputSize += (PW_k * 0x100);
  /* nbr of waveforms */
  PW_k = ((in_data[PW_Start_Address + 0x40]*256*256*256)+
	  (in_data[PW_Start_Address + 0x41]*256*256)+
	  (in_data[PW_Start_Address + 0x42]*256)+
	  in_data[PW_Start_Address + 0x43]);
  OutputSize += (PW_k * 0x80);
  /* nbr of sequences */
  PW_l = 0;
  for (PW_j=0;PW_j<8;PW_j++)
  {
    PW_k = ((in_data[PW_Start_Address + 0x1c + (PW_j*4)]*256*256*256)+
	    (in_data[PW_Start_Address + 0x1d + (PW_j*4)]*256*256)+
	    (in_data[PW_Start_Address + 0x1e + (PW_j*4)]*256)+
	    in_data[PW_Start_Address + 0x1f + (PW_j*4)]);
    PW_l += PW_k;
  }
    
  OutputSize += (PW_l * 8);

  CONVERT = BAD;
  Save_Rip ( "Digital Mugician 1/2 module", dmu );
  
  if ( Save_Status == GOOD )
    PW_i += 24; /* after the ID */
}
