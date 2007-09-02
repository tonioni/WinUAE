/* Viruz2 - made by kb_ Farbrausch */
/* 06-05-2007 - test version by Muerto - mostly asle :D*/
/* 08-05-2007 - Saved the first V2m mod!! - BUT phoemes not incluted.. yet!*/
/* testV2 */

/* This is an ongoing dev version made by Muerto - not part of PW4PC package */

#include "globals.h"
#include "extern.h"

/* void testViruz2_60 (void)*/
short testViruz2_60 ()
{
  PW_Start_Address = PW_i; /* bypass the 20 bytes already tested */

  for (PW_j = PW_Start_Address+20; PW_j<(PW_in_size-12); PW_j++)
  {
    /* PW_in_size if the size of the input file */
               
    if ((in_data[PW_j+1] == 0x00) &&
       (in_data[PW_j+2] == 0x00) &&
       (in_data[PW_j+3] == 0x00) &&
       ((in_data[PW_j+4] == 0x01) || (in_data[PW_j+4] == 0x1F)) &&
       (in_data[PW_j+5] == 0x00) &&
       (in_data[PW_j+6] == 0x00) &&
       (in_data[PW_j+7] == 0x00) &&
       ((in_data[PW_j+8] == 0x08) || (in_data[PW_j+8] == 0x26)) &&
       (in_data[PW_j+9] == 0x00) &&
       (in_data[PW_j+10] == 0x00) &&
       (in_data[PW_j+11] == 0x00) )
    {
      /* printf ( "\n found end tag for 6000 at %ld (%lx)\n",PW_j, PW_j ); */
      /*OutputSize is the global PW var to store the output size, used by Save_Rip*/ 
      OutputSize = (PW_j+12)-PW_Start_Address;
      CONVERT = BAD; /* tells PW not to consider converting to PTK */
      Save_Rip ( "Viruz 2", Viruz2 );
      if ( Save_Status == GOOD )
        PW_i += 1;  /* to bypass the case test */
    }
  }
   return GOOD;
}

short testViruz2_80 ()
{
  Uchar MYTEST = BAD;
  PW_Start_Address = PW_i; /* bypass the 20 bytes already tested */

  /* test 1 */
  for (PW_j = PW_Start_Address+20; PW_j<(PW_in_size-12); PW_j++)
  {
    /* PW_in_size if the size of the input file */
        /* Test before @@@ */       
    if ((in_data[PW_j] == 0x16) &&
       (in_data[PW_j+1] == 0x00) &&
       (in_data[PW_j+2] == 0x00) &&
       (in_data[PW_j+3] == 0x00) &&
       ((in_data[PW_j+8] == 0x64) || (in_data[PW_j+8] == 0x6B) || (in_data[PW_j+8] == 0x6E) || (in_data[PW_j+8] == 0x7F) || (in_data[PW_j+8] == 0x5B) || (in_data[PW_j+8] == 0x60) || (in_data[PW_j+8] == 0x19) || (in_data[PW_j+8] == 0x66) || (in_data[PW_j+8] == 0x58) || (in_data[PW_j+8] == 0x0E) || (in_data[PW_j+8] == 0x5A) ) &&
       ((in_data[PW_j+17] == 0x00) || (in_data[PW_j+17] == 0x01) || (in_data[PW_j+17] == 0x02) || (in_data[PW_j+17] == 0x1F)) &&
       ((in_data[PW_j+18] == 0x00) || (in_data[PW_j+18] == 0x01) || (in_data[PW_j+18] == 0x33)) &&
       ((in_data[PW_j+19] == 0x00) || (in_data[PW_j+19] == 0x01) || (in_data[PW_j+19] == 0x02) || (in_data[PW_j+19] == 0x36)))

       {
         MYTEST = GOOD;
         printf ("first row of test OK (where : %ld)\n",PW_j);
         break;
       }
  }
  if (MYTEST == GOOD)
  { /* test 1 OK - proceed to test 2, then save if successful */
    while (PW_j<(PW_in_size-12))
    {
         /* test after @@@ */
      if ((in_data[PW_j] == 0xF8) &&
         (in_data[PW_j+1] == 0x02) &&
         (in_data[PW_j+2] == 0x00) &&
         (in_data[PW_j+3] == 0x00) &&
         (in_data[PW_j+4] == 0x86) &&
         (in_data[PW_j+5] == 0x03) &&
         (in_data[PW_j+6] == 0x00) &&
         (in_data[PW_j+7] == 0x00) &&
         (in_data[PW_j+8] == 0xA1) &&
         (in_data[PW_j+9] == 0x04) &&
         (in_data[PW_j+10] == 0x00) &&
         (in_data[PW_j+11] == 0x00))   
       
      {
        /* printf ( "\n found end tag for 8000 at %ld (%lx)\n",PW_j, PW_j ); */
        /*OutputSize is the global PW var to store the output size, used by Save_Rip*/ 
        OutputSize = (PW_j+12)-PW_Start_Address;
        CONVERT = BAD; /* tells PW not to consider converting to PTK */
        Save_Rip ( "Viruz 2", Viruz2 );
        if ( Save_Status == GOOD )
          PW_i += 1;  /* to bypass the case test */
      }
      PW_j++;
    }
  }
   return GOOD;
}

short testViruz2_E0 ()
{
  PW_Start_Address = PW_i; /* bypass the 20 bytes already tested */

  for (PW_j = PW_Start_Address+20; PW_j<(PW_in_size-12); PW_j++)
  {
    /* PW_in_size if the size of the input file */
               
    if ((in_data[PW_j] == 0x94) &&
       ((in_data[PW_j+1] == 0x00) || (in_data[PW_j+1] == 0x01)) &&
       (in_data[PW_j+2] == 0x00) &&
       (in_data[PW_j+3] == 0x00) &&
       ((in_data[PW_j+4] == 0x01) || (in_data[PW_j+4] == 0x78) || (in_data[PW_j+4] == 0x81) || (in_data[PW_j+4] == 0xDD)) && 
       ((in_data[PW_j+5] == 0x00) || (in_data[PW_j+5] == 0x01)) && 
       (in_data[PW_j+6] == 0x00) &&
       (in_data[PW_j+7] == 0x00) &&
       ((in_data[PW_j+8] == 0x08) || (in_data[PW_j+8] == 0xC2) || (in_data[PW_j+8] == 0x27) || (in_data[PW_j+8] == 0xC8) || (in_data[PW_j+8] == 0x01) ) && 
       ((in_data[PW_j+9] == 0x00) || (in_data[PW_j+9] == 0x01)) && 
       (in_data[PW_j+10] == 0x00) &&
       (in_data[PW_j+11] == 0x00) )
    {
      /* printf ( "\n found end tag for E001 at %ld (%lx)\n",PW_j, PW_j ); */
      /*OutputSize is the global PW var to store the output size, used by Save_Rip*/ 
      OutputSize = (PW_j+12)-PW_Start_Address;
      CONVERT = BAD; /* tells PW not to consider converting to PTK */
      Save_Rip ( "Viruz 2", Viruz2 );
      if ( Save_Status == GOOD )
        PW_i += 1;  /* to bypass the case test */
    }
  }
   return GOOD;
}

/* end of func */

