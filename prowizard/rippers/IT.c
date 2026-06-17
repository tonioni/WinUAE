/* testIT() */
/* Rip_IT() */

/*
first try : 20110808
*/


#include "globals.h"
#include "extern.h"


int16_t	 testIT ( void )
{
  /* test #1 */
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address + 0xc0) > PW_in_size)
  {
    /*printf ( "#1 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  /* must be < 10 */
  if ( (in_data[PW_Start_Address + 41] >= 0x0f) || (in_data[PW_Start_Address + 43] >= 0x0F) )
  {
    /*printf ( "#2 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  PW_k = 0xc0 + (in_data[PW_Start_Address + 33]*256)+in_data[PW_Start_Address + 32];
  if ( PW_k > PW_in_size)
  {
    /*printf ( "#3 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_j = PW_k + ((in_data[PW_Start_Address + 35]*256)+in_data[PW_Start_Address + 34])*4;
  if ( PW_j > PW_in_size)
  {
    /*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }
  PW_l = PW_j + ((in_data[PW_Start_Address + 37]*256)+in_data[PW_Start_Address + 36])*4;
  if ( PW_l > PW_in_size)
  {
    /*printf ( "#4 Start:%ld\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
/* PW_k => inst addresses*/
/* PW_j => smp headers addresses*/
/* PW_l => patterns addresses */
}


/*
 * IT ripper
 * 20110808 - Sylvain "Asle" Chipaux
 *
 * let's try it
 * OK, what's is the size of one compressed sample ?!?!?
*/
void Rip_IT ( void )
{
/*
from the test part -> 
 PW_k => inst addresses
 PW_j => smp headers addresses
 PW_l => patterns addresses
*/

  int32_t	 PtrInst = PW_k;
  int32_t	 NbrInst = (in_data[PW_Start_Address + 35]*256)+in_data[PW_Start_Address + 34];
  int32_t	 LenInst = 0;
  int32_t	 PtrSmpHead = PW_j;
  int32_t	 NbrSmpHead = (in_data[PW_Start_Address + 37]*256)+in_data[PW_Start_Address + 36];
/*  int32_t	 LenSmpHead = 0;*/
  int32_t	 PtrPatt = PW_l;
/*  int32_t	 NbrPatt = (in_data[PW_Start_Address + 39]*256)+in_data[PW_Start_Address + 38];*/
/*  int32_t	 LenPatt = 0;*/
  int32_t	 currentptr;
  int32_t	 max = 0;
/*  int32_t	 whole_inst_size=0;*/
/*  int32_t	 whole_head_size=0;*/
  uint8_t COMPRESSED = 1;

  printf ("\nPtrInst:%x\nPtrSmpHead:%x\nPtrPatt:%x\n",PtrInst,PtrSmpHead,PtrPatt);

  /* let's get the highest address of an instrument (if any is set) */
  if (NbrInst > 0)
  {
    currentptr = PW_Start_Address + PtrInst;
    for ( PW_k=0 ; PW_k<NbrInst ; PW_k++ )
    {
      int32_t	 tmp_addy = ((in_data[currentptr+(PW_k*4)+3]*256*256*256) +
                 (in_data[currentptr+(PW_k*4)+2]*256*256) +
                 (in_data[currentptr+(PW_k*4)+1]*256) +
                 in_data[currentptr+(PW_k*4)]);
      /*printf ("inst addy[%lx]:%lx\n",PW_k,tmp_addy);*/
      if (tmp_addy == 0)
        continue;
      if ( tmp_addy > max )
      {
        max = tmp_addy + 554;
      }
      /* inst have constant size */

      LenInst = NbrInst*554;

    }
  }

  /* let's get the highest address of a sample (if any is set) */
  if (NbrSmpHead > 0)
  {
    currentptr = PW_Start_Address + PtrSmpHead;
    for ( PW_k=0 ; PW_k<NbrSmpHead ; PW_k++ )
    {
        /* address of smaple header */
      int32_t	 tmp_addy = ((in_data[currentptr+(PW_k*4)+3]*256*256*256) +
                 (in_data[currentptr+(PW_k*4)+2]*256*256) +
                 (in_data[currentptr+(PW_k*4)+1]*256) +
                 in_data[currentptr+(PW_k*4)]);
        /* then address of its sample data */
      int32_t	 tmp_addy2 = (in_data[PW_Start_Address+tmp_addy+0x4b]*256*256*256)+
           (in_data[PW_Start_Address+tmp_addy+0x4a]*256*256)+
           (in_data[PW_Start_Address+tmp_addy+0x49]*256)+
           in_data[PW_Start_Address+tmp_addy+0x48];
      printf ("smp addy[%2x]:%x (max:%x)(at:%x) ",PW_k,tmp_addy,max,tmp_addy2);
      if (tmp_addy == 0)
        continue;
      if (tmp_addy2 == 0)
      {
        printf ("---");
      }
      if ( tmp_addy > max )
        max = tmp_addy+0x50;
      if ( tmp_addy2 >= max )
      {
        int32_t	 tmp_size = (in_data[PW_Start_Address+tmp_addy+0x33]*256*256*256)+
             (in_data[PW_Start_Address+tmp_addy+0x32]*256*256)+
             (in_data[PW_Start_Address+tmp_addy+0x31]*256)+
             in_data[PW_Start_Address+tmp_addy+0x30];
        if ((in_data[PW_Start_Address+tmp_addy+0x12]&0x02) == 0x02) /* 16 bits ? */
          tmp_size *= 2;
        /* OK, what's the size of 'compressed' samples ?!? */
        if ((in_data[PW_Start_Address+tmp_addy+0x12]&0x08) == 0x08) /* compressed ? */
        {
          printf ("*");
          COMPRESSED = 2;
          tmp_size /= 2; /* just to be sure */
        }
        else
          COMPRESSED = 1;
        printf ("- size:%x)",tmp_size);
        max = tmp_addy2+tmp_size;
      }
      printf ("\n");

      /* here, max is now after the farthest sample data */

    }
  }

/* PATTERNS could be _after_ the samples ... never seen but ...
shall have to be handled, some day */

  OutputSize = max;

  CONVERT = BAD;
  if (COMPRESSED == 2)
  {
    printf ("found Impulse Tracker module at %d with last sample compressed - can't save\n",PW_Start_Address);
    Save_Status = GOOD;
  }
  else
  {
    Save_Rip ( "Impulse Tracker module", ImpulseTracker );
  }
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}
