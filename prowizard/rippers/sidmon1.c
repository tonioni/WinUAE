/*
  thanks to Markus
  20091124 - Asle
*/
/* testSIDMON1() */
/* Rip_SIDMON1() */



#include "globals.h"
#include "extern.h"


int16_t	 testSIDMON1 ( void )
{
  PW_Start_Address = PW_i;

  /* file size < 4 */
  if ( (PW_in_size - PW_Start_Address) < 4 )
  {
    /*printf ( "#1 (start:%ld) (size:%ld)\n" , PW_Start_Address , PW_in_size-PW_Start_Address);*/
    return BAD;
  }

  /* get central addy */
  PW_m = (( in_data[PW_Start_Address+2]*256)+
            in_data[PW_Start_Address+3] );


  /* test in-size again */
  if ( PW_Start_Address+PW_m+2 > PW_in_size )
  {
    /*printf ( "#2 (start:%ld) (central addy:%ld)\n" , PW_Start_Address , PW_m);*/
    return BAD;
  }

  /* test some bytes */
  if ( (in_data[PW_Start_Address+6] != 0xFF) || (in_data[PW_Start_Address+7] != 0xD4) )
  {
    /*printf ( "#3 (start:%ld)\n" , PW_Start_Address);*/
    return BAD;
  }

  /* get tech addys */
  PW_n = (( in_data[PW_Start_Address+PW_m-2+2]*256)+ /* smps desc */
            in_data[PW_Start_Address+PW_m-1+2] );
  PW_o = (( in_data[PW_Start_Address+PW_m-6+2]*256)+ /* patlist addy */
            in_data[PW_Start_Address+PW_m-5+2] );
  PW_j = (( in_data[PW_Start_Address+PW_m-10+2]*256)+ /* pat data addy */
            in_data[PW_Start_Address+PW_m-9+2] );

  /* test in-size again */
  if ( (PW_Start_Address+PW_m+2+PW_n+4 > PW_in_size ) ||
       (PW_Start_Address+PW_m+2+PW_o+4 > PW_in_size ) )
  {
    /*printf ( "#4 (start:%ld) (PW_n:%ld) (PW_o:%ld) (PW_j:%ld)\n" , PW_Start_Address , PW_n,PW_o,PW_j);*/
    return BAD;
  }

  /* check both adresses consistency */
  if ( (PW_n <= PW_o) && (PW_n != 1) )
  {
    /*printf ( "#5 (start:%ld) (smp addy:%ld) (patlist addy:%ld) (PW_m:%ld)\n" , PW_Start_Address , PW_n, PW_o,PW_m);*/
    return BAD;
  }

  /* no sample - then check only patlist at the end */
  if (PW_n == 1)
  {
    PW_k = PW_Start_Address+PW_m+2+PW_o;
    if ( (PW_k + 4) > PW_in_size ) /* one pattern ? odd but ... */
       return GOOD;
    PW_k += 4;
    PW_l = 1;
    PW_n = PW_o - PW_j;
    /*printf ("(PW_k:%ld)(PW_n:%ld)\n",PW_k,PW_n);*/
    while(PW_l < PW_n)
    {
      /*printf ("%lx - \n", PW_l);*/
      PW_l = (( in_data[PW_k]*256*256*256)+
              ( in_data[PW_k+1]*256*256)+
              ( in_data[PW_k+2]*256)+
                in_data[PW_k+3] );
      if ( ((PW_k + 4) > PW_in_size ) || (PW_l == 0)) /* end of file, so end of patlist .. or 0 */
         return GOOD;
      PW_k += 4;
    }
    PW_k -= 4;
  }
  else /* there are some samples */
  {
    PW_n = PW_Start_Address+PW_m+2+PW_n;
    PW_m = (in_data[PW_n+2]*256) + in_data[PW_n+3];
    if ( ((PW_n+PW_m) > PW_in_size) || ((PW_m%32) != 0) )
    {
      /*printf ( "#6 (start:%ld) (size of smp desc:%ld)(PW_n:%ld)\n" , PW_Start_Address , PW_m,PW_n);*/
      return BAD;
    }
    if ( PW_m == 0 )
    { /* weird hack ?!? try to guess the smp desc size */
      for ( ; ;PW_m += 32 )
      {
        /*printf ("+");*/
        if (PW_m + 4 + 32 > PW_in_size)
        {
          /*printf ( "#6,1 (start:%ld) (size of smp desc:%ld)(PW_n:%ld)\n" , PW_Start_Address , PW_m,PW_n);*/
          return BAD;
        }
        PW_j = (( in_data[PW_n+PW_m+4]*256*256*256)+
                ( in_data[PW_n+PW_m+5]*256*256)+
                ( in_data[PW_n+PW_m+6]*256)+
                  in_data[PW_n+PW_m+7] );
        PW_k = (( in_data[PW_n+PW_m+8]*256*256*256)+
                ( in_data[PW_n+PW_m+9]*256*256)+
                ( in_data[PW_n+PW_m+10]*256)+
                  in_data[PW_n+PW_m+11] );
        PW_l = (( in_data[PW_n+PW_m+12]*256*256*256)+
                ( in_data[PW_n+PW_m+13]*256*256)+
                ( in_data[PW_n+PW_m+14]*256)+
                  in_data[PW_n+PW_m+15] );
        if ((PW_j > PW_k) || (PW_k >= PW_l))
        {
          /*printf ("%lx,%lx,%lx\n",PW_j,PW_k,PW_l);*/
          break;
        }
      }
    }
    /*printf ("\n(PW_n:%ld)(PW_m:%ld)\n",PW_n,PW_m);*/
    /* get biggest end addy of samples */
    PW_o = 0;
    for ( PW_j=0; PW_j<(PW_m/32); PW_j+=1)
    {
      /*printf ("%lx - \n",PW_o);*/
      PW_k = ((in_data[PW_n+14+PW_j*32]*256) + in_data[PW_n+15+PW_j*32]);
      if (PW_k > PW_o)
        PW_o = PW_k;
    }
    PW_n += (PW_m + PW_o + 4);
  }

  return GOOD;
}



void Rip_SIDMON1 ( void )
{

  OutputSize = PW_n - PW_Start_Address;

  CONVERT = BAD;
  Save_Rip ( "Sidmon v1 module", Sidmon1 );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}

