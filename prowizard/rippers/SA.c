/*
  Sonic Arranger ("shot in the dark" try)
  20160313- Asle
20160314: no hunk version
*/
/* testSAhunk() */
/* Rip_SAhunk() */



#include "globals.h"
#include "extern.h"


int16_t	 testSAhunk ( void )
{
  PW_Start_Address = PW_i;

  if ( (in_data[PW_Start_Address+8] != 'S' ) ||
       (in_data[PW_Start_Address+9] != 'T' ) ||
       (in_data[PW_Start_Address+10] != 'B' ) ||
       (in_data[PW_Start_Address+11] != 'L' ) )
  {
    printf ( "#1 Start:%d - expecting STBL hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* file size */
  if ( (PW_in_size - PW_Start_Address) < 36 )
  {
    /*printf ( "#1,1 (start:%ld) (size:%ld)\n" , PW_Start_Address , PW_in_size-PW_Start_Address);*/
    return BAD;
  }

  /* get nbr patterns */
  /*PW_m = (( in_data[PW_Start_Address+18]*256)+
            in_data[PW_Start_Address+19] );*/

/************* OVTB ****************/
  if ( (in_data[PW_Start_Address+28] != 'O' ) ||
       (in_data[PW_Start_Address+29] != 'V' ) ||
       (in_data[PW_Start_Address+30] != 'T' ) ||
       (in_data[PW_Start_Address+31] != 'B' ) )
  {
    printf ( "#1,2 Start:%d - expecting OVTB hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* nbr of Overtable */
  PW_n = (( in_data[PW_Start_Address+34]*256)+
            in_data[PW_Start_Address+35] )*16;

  /* file size */
  if ( (PW_in_size - PW_Start_Address - 32) < (PW_n + 8) )
  {
    /*printf ( "#1,3 (start:%ld) (size:%ld)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-32);*/
    return BAD;
  }

  PW_n += 36; /* should now be on NTBL hunk */

/************* NTBL ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'N' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'T' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'B' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'L' ) )
  {
    printf ( "#2 Start:%d - expecting NTBL hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* nbr of notes */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] )*4;

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#2,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on INST hunk */

/************* INST ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'I' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'N' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'S' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'T' ) )
  {
    printf ( "#3 Start:%d - expecting INST hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* nbr of instruments */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] )*152;

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#3,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on SD8B hunk */

/************* SD8B ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'S' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'D' ) ||
       (in_data[PW_Start_Address+PW_n+2] != '8' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'B' ) )
  {
    printf ( "#4 Start:%d - expecting SD8B hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* nbr 8b samples */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] );

  /* sample data size, if any */
  if (PW_m>0)
  {
    PW_k = 0;
    PW_l = (PW_m * 4) + (PW_m * 4) + (PW_m * 30);
    for (PW_o=0; PW_o<PW_m; PW_o++)
    {
      PW_j = (in_data[PW_Start_Address+PW_n+PW_l+(PW_o*4)+10]*256 + in_data[PW_Start_Address+PW_n+PW_l+(PW_o*4)+11]);
      PW_k += PW_j;
      /*printf ("(at:%d)(PW_j=%d)\n",PW_Start_Address+PW_n+PW_l+(PW_o*4)+10,PW_j);*/
    }
    PW_l += (PW_m * 4);
    PW_m = PW_l + PW_k;
  }

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#4,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on SYWT hunk */


/************* SYWT ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'S' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'Y' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'W' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'T' ) )
  {
    printf ( "#5 Start:%d (at %d) (PW_l:%d) - expecting SYWT hunk\n" , PW_Start_Address, PW_Start_Address + PW_n,PW_l );
    return BAD;
  }

  /* nbr synth wave tables */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] )*128;

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#5,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on SYAR hunk */

/************* SYAR ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'S' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'Y' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'A' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'R' ) )
  {
    printf ( "#6 Start:%d (at %d) - expecting SYAR hunk\n" , PW_Start_Address, PW_Start_Address + PW_n );
    return BAD;
  }

  /* nbr synth wave tables */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] )*128;

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#6,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on SYAF hunk */

/************* SYAF ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'S' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'Y' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'A' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'F' ) )
  {
    printf ( "#7 Start:%d (at %d) - expecting SYAF hunk\n" , PW_Start_Address, PW_Start_Address + PW_n  );
    return BAD;
  }

  /* nbr synth wave tables */
  PW_m = (( in_data[PW_Start_Address+PW_n+6]*256)+
            in_data[PW_Start_Address+PW_n+7] ); /* no known case, so size of block is unknown */
  if (PW_m > 0)printf ("testSA() - case of SYAF that is not 0 - send this file to Asle\n");

  /* file size */
  if ( (PW_in_size - PW_Start_Address - PW_n) < (PW_m + 16) )
  {
    printf ( "#7,1 (start:%d) (size:%d)\n" , PW_Start_Address , PW_in_size-PW_Start_Address-PW_n);
    return BAD;
  }

  PW_n += (PW_m+8);  /* should now be on EDATV1.1 hunk */

/************* EDATV1.1 ****************/
  if ( (in_data[PW_Start_Address+PW_n] != 'E' ) ||
       (in_data[PW_Start_Address+PW_n+1] != 'D' ) ||
       (in_data[PW_Start_Address+PW_n+2] != 'A' ) ||
       (in_data[PW_Start_Address+PW_n+3] != 'T' ) )
  {
    printf ( "#8 Start:%d - expecting EDATV1.1 hunk\n" , PW_Start_Address );
    return BAD;
  }

  /* nbr synth wave tables */
  PW_m = 16; /* always 16 bytes, it seems */

  PW_n += (PW_m+8);  /* should now be at the end */

  return GOOD; 
}



/*****************************************************************************/
/*
Sonic Arranger - no humk
*/
int16_t	testSA ( void )
{
  PW_Start_Address = PW_i;

  PW_n = 3;
  if ( in_data[PW_Start_Address+PW_n] >0x7f )
  {
    /*printf ( "#1 Start:%d - jump too far\n" , PW_Start_Address );*/
    return BAD;
  }

  /* file size */
  if ( (PW_Start_Address + in_data[PW_Start_Address+PW_n]) > PW_in_size )
  {
    /*printf ( "#1,1 (start:%d) (jump:%d)\n" , PW_Start_Address , in_data[PW_Start_Address+PW_n]);*/
    return BAD;
  }


/************* point after replay ****************/
  PW_n = (( in_data[PW_Start_Address+22]*256)+
            in_data[PW_Start_Address+23] );
  PW_n += 22;

  /* file size */
  if ( (PW_Start_Address + PW_n) > PW_in_size )
  {
    printf ( "#1,3 (start:%d) (jump:%d)\n" , PW_Start_Address , PW_Start_Address + PW_n);
    return BAD;
  }
printf ("testSA(%x) - jump to %x\n",PW_Start_Address,PW_n);

/************* try to detect the next 'Nu' ni the next 100h bytes ****************/
  for (PW_j=0; PW_j<0x100; PW_j++)
  {
    if ((in_data[PW_Start_Address + PW_n + PW_j] == 'N') &&
        (in_data[PW_Start_Address + PW_n + PW_j + 1] == 'u'))
      break;
  }
  if (PW_j == 0x100)
  {
    printf ( "#2 Start:%d - no 'Nu' found\n" , PW_Start_Address );
    return BAD;
  }
printf ("testSA(%x) - 'Nu' at %x\n",PW_Start_Address,PW_j);

/************* try to detect the next usable dword ****************/
  PW_n += (PW_j + 2);
  if ((in_data[PW_Start_Address + PW_n+3] == 0x00) &&
      (in_data[PW_Start_Address + PW_n+5] == 0x00) )
  {
    printf ( "#3 Start:%d (at %x) - consecutive 0x00 found\n" , PW_Start_Address, PW_n+PW_Start_Address );
    return BAD;
  }
  if (in_data[PW_Start_Address + PW_n+5] != 0x00)
    PW_n += 2;
printf ("testSA(%x) - first dword at %x\n",PW_Start_Address,PW_n);
    
/************* real music data start here ****************/

  /* file size */
  if ( (PW_Start_Address + PW_n + 32) > PW_in_size )
  {
    printf ( "#4 (start:%d) (size:%d)\n" , PW_Start_Address , PW_Start_Address + PW_n + 32);
    return BAD;
  }

/*************  pseudo SD8B hunk ****************/

  PW_j = (( in_data[PW_Start_Address+PW_n+30]*256)+
            in_data[PW_Start_Address+PW_n+31] );
  PW_n += PW_j; /* this includes the 32 bytes of addresses */
printf ("testSA(%x) - S8BD hunk at %x\n",PW_Start_Address,PW_n);

  /* nbr 8b samples */
  PW_m = (( in_data[PW_Start_Address+PW_n+2]*256)+
            in_data[PW_Start_Address+PW_n+3] );
printf ("testSA(%x) - nbr 8b data %x\n",PW_Start_Address,PW_m);
  PW_n += 4;
/*printf ("at %x (PW_m:%x)\n",PW_n+PW_Start_Address,PW_m);fflush (stdout);*/

  /* sample data size, if any */
  if (PW_m>0)
  {
    PW_k = 0;
    for (PW_o=0; PW_o<PW_m; PW_o++)
    {
      PW_j = (in_data[PW_Start_Address+PW_n+(PW_o*4)+2]*256 + in_data[PW_Start_Address+PW_n+(PW_o*4)+3]);
printf ("testSA(%x) - 8b data sizes %x\n",PW_Start_Address,PW_j);
      PW_k += PW_j;
    }
  }

  PW_n += (PW_k+(PW_m*4));  /* should now be on 'deadbeef' tag */
/*printf ("at %x\n",PW_n+PW_Start_Address);fflush (stdout);*/

/* OK, detect 0xF500 after the 'deadbeef' hunk size */
  for (PW_j=12;PW_j<0x100;PW_j++)
  {
    if ((in_data[PW_Start_Address+PW_n+PW_j] == 0xF5)&&
        (in_data[PW_Start_Address+PW_n+PW_j+1] == 0x00))
    {
      PW_n += (PW_j+2);
      printf ("0xF500 found at %x (%x)\n",PW_j,PW_n);
      break;
    }
    if ((in_data[PW_Start_Address+PW_n+PW_j])>PW_in_size)
    {
      printf ("no 0xF500 found. size will be at PW_n (%x)\n",PW_n);
      break;
    }
  }


  return GOOD; 
}

void Rip_SA ( void )
{

  OutputSize = PW_n;

  CONVERT = BAD;
  Save_Rip ( "Sonic Arranger module", SA );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}

