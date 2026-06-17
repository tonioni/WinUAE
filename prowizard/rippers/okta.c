/* testOkta() */
/* Rip_Okta() */

#include "globals.h"
#include "extern.h"


int16_t	 testOkta ( void )
{
  /* test 1 */
  PW_Start_Address = PW_i;
  PW_m = PW_Start_Address;
  PW_m += 12; /* after OKTASONGCMOD */

  /* CMOD chunk size */
  PW_j = (in_data[PW_m]*256*256*256)+
         (in_data[PW_m+1]*256*256)+
         (in_data[PW_m+2]*256)+
         in_data[PW_m+3];
  if ((PW_j > 8) || (PW_j<4)) /* never seen it being different than 8 */
  {
/*    printf ("invalid number of voices : %l\n",PW_j);*/
    return BAD;
  }

  PW_m += PW_j+4;
  /*should now be on SAMP*/
  if ((in_data[PW_m]!='S')||
      (in_data[PW_m+1]!='A')||
      (in_data[PW_m+2]!='M')||
      (in_data[PW_m+3]!='P'))
  {
/*    printf ("bad tag. expecting SAMP (start:%ld)\n",PW_Start_Address);*/
    return BAD;
  }

  PW_m +=4;
  /* SAMP chunk size */
  PW_j = (in_data[PW_m]*256*256*256)+
         (in_data[PW_m+1]*256*256)+
         (in_data[PW_m+2]*256)+
         in_data[PW_m+3];
  if (PW_j != 0x480) /* size fixed as specified */
  {
/*    printf ("invalid SAMP chunk size : %lx\n",PW_j);*/
    return BAD;
  }
  PW_m +=4;

  /* PW_m is the first sample now */
  return GOOD;
}



void Rip_Okta ( void )
{
  int32_t	 Where=PW_m;
/*printf ( "Where : %ld\n",Where);*/

  /*get the number of samples */
  PW_l = 0;
  for (PW_k=0; PW_k<36; PW_k++)
  {
    PW_m = (in_data[Where+20+PW_k*32]*256*256*256)+
         (in_data[Where+21+PW_k*32]*256*256)+
         (in_data[Where+22+PW_k*32]*256)+
         in_data[Where+23+PW_k*32];
    if (PW_m != 0)
      PW_l += 1; /* so PW_l holds the nbr of samples (i.e. SBOD)*/
  }
  /* bypass SAMP */
  Where += 0x480;

  /* bypass SPEE entire chunk, which total size should be 4 + 4 + 2 */
  Where += +10;
  
  /* bypass SLEN word and its size which is always 2 */
  Where += 8;
  
  /* get song length (i.e. number of PBOD) */
  PW_m = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  
  /* bypass PLEN entire chunk which size is always 4 + 4 + 2 */
  Where += 10;
  
  /* bypass PATT entire chunk which size is always 128 + 4 + 4 */
  Where += 136;

  /* now, lets calculate the size of patterns (PBODs) */
  for (PW_k=0; PW_k<PW_m; PW_k++)
  {
    Where += 4; /*bypass 'PBOD'*/
    PW_j = (in_data[Where]*256*256*256)+
           (in_data[Where+1]*256*256)+
           (in_data[Where+2]*256)+
           in_data[Where+3];
    Where += 4; /* bypass the dword just read */
    Where += PW_j;
    if (Where > PW_in_size)
      break;
/*printf ( "Where : %ld\n",Where);*/
  }
  /* Where is now on the first SBOD */

  /* now, lets calculate the size of samples (SBODs) */
  for (PW_k=0; PW_k<PW_l; PW_k++)
  {
    Where += 4; /*bypass 'SBOD'*/
    PW_j = (in_data[Where]*256*256*256)+
           (in_data[Where+1]*256*256)+
           (in_data[Where+2]*256)+
           in_data[Where+3];
    Where += 4; /* bypass the dword just read */
    Where += PW_j;
    if (Where > PW_in_size)
    {
      PW_o = BAD;
      break;
    }
  }
  /* We're is now at the end of the OKTA file */
    

  if (PW_o != BAD)
  { 
    OutputSize = Where - PW_Start_Address;
  
    CONVERT = BAD;
    Save_Rip ( "Oktalizer module", Oktalizer );
    
    if ( Save_Status == GOOD )
      PW_i += 1;
  }
  else
  {
    printf ("found a too much truncated Oktalizer file\n");
  }
}

