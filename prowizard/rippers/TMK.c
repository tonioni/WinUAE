/* testTMK() */
/* Rip_TMK() */
/* Depack_TMK() */

#include "globals.h"
#include "extern.h"



short testTMK ( void )
{
  /* test 1 */
  PW_Start_Address = PW_i;
  if ( (PW_Start_Address+100) > PW_in_size )
    return BAD;

  /* test 2  - nbr of smps*/
  PW_j = in_data[PW_Start_Address + 5]&0x7f;
  if ( PW_j > 0x1f )
    return BAD;

  /* test 2 - size of patlist */
  PW_l = in_data[PW_Start_Address + 4];
  if ( PW_l > 0x80 )
    return BAD;

  /* test 2,5 - out of in_file ? */
  if ( PW_Start_Address + 6 + (PW_j*8) + PW_l > PW_in_size )
    return BAD;

  /* test 3 - fine */
  for ( PW_k=(PW_Start_Address+12);PW_k<PW_j*8;PW_k+=8)
    if (in_data[PW_k]>0x0f)
      return BAD;

  /* test 4 - vol */
  for ( PW_k=(PW_Start_Address+13);PW_k<PW_j*8;PW_k+=8)
    if (in_data[PW_k]>0x40)
      return BAD;

  /* get all smp size */
  PW_WholeSampleSize = 0;
  for (PW_k=PW_Start_Address+6;PW_k<PW_Start_Address+6+PW_j*8;PW_k+=8)
    PW_WholeSampleSize += (((in_data[PW_k]*256)+in_data[PW_k+1])*2);

  /* test 5 - max of line pointer */
  /* first, max of patlist */
  PW_m = 0;
  for ( PW_k=PW_Start_Address+6+(8*PW_j); PW_k<(PW_Start_Address+6+(8*PW_j)+PW_l);PW_k++)
    if(in_data[PW_k] > PW_m)
      PW_m = in_data[PW_k]/2 + 1;

  if ( (PW_m*64*2)+6+(8*PW_j)+PW_l+PW_Start_Address > PW_in_size )
    return BAD;

  return GOOD;
}




void Rip_TMK ( void )
{
  /* PW_j is the number of samples - useless*/
  /* PW_l is the patlist size */
  /* PW_WholeSampleSize is the whole sample size :) */
  /* PW_m is the highest pat nbr */

  /* get highest ref addy */
  /* also taking care of the uneven patlist size */
  if ((PW_l/2)*2 != PW_l)
    PW_l += 1;
  PW_n = 6+(8*PW_j)+PW_l+PW_Start_Address;
  /*printf ("\nsize of header (PW_n) : %ld (%lx)\n",PW_n,PW_n);*/
  /* now PW_n points on the first referencer */
  /* hence PW_j and PW_l are free */
  PW_l = 0;
  for (PW_k=PW_n;PW_k<PW_n+(PW_m*128);PW_k+=2)
  {
    PW_o = ((in_data[PW_k]*256)+in_data[PW_k+1]);
    if (PW_o>PW_l)
      PW_l = PW_o;
  }
  /* PW_l is now the highest ref pointer */
  /*printf ("highest ref number (PW_l):%ld (%lx)\n",PW_l,PW_l);*/
  /* remain to depack the last line */

  PW_o = 0;
  for ( PW_j=0; PW_j<4; PW_j++ )
  {
    if ( in_data[PW_l+PW_n+PW_o] == 0x80 ){PW_o+=1;continue;}
    if ( (in_data[PW_l+PW_n+PW_o] & 0x80) == 0x80 ){PW_o+=2;continue;}
    PW_o += 3;
  }
  /*printf ("last line size (PW_l) : %ld (%lx)\n",PW_o,PW_o);*/

  OutputSize = PW_WholeSampleSize + PW_n + PW_l + PW_o - PW_Start_Address;

  CONVERT = GOOD;
  Save_Rip ( "TMK module", TMK );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1);  /* 0 should do but call it "just to be sure" :) */
}



/*
 *   tmk.c   2003 (c) sylvain "Asle" Chipaux
 *
 * Converts TMK packed MODs back to PTK MODs
 * 20040906 - added the uneven case in the patternlist
 *
*/

#define ON  0
#define OFF 1

#define PATTERN_DATA_ADDY 5222

void Depack_TMK ( void )
{
  Uchar *Pattern;
  long i=0,j=0,k=0,l=0;
  long Total_Sample_Size=0;
  Uchar poss[37][2];

  int   patdataaddy;
  int   reftableaddy;
  int   maxlineaddy;
  int   sizlastline;
  long  *samplesizes;
  Uchar patternlist[128];
  Uchar maxpat = 0x00;
  Uchar NOP=0x00; /* number of pattern */
  Uchar NOS=0x00; /* number of sample */
  Uchar *Whatever;
  Uchar GLOBAL_DELTA = OFF;
  long Where = PW_Start_Address;
  FILE *out;/*,*info;*/

  fillPTKtable(poss);

  if ( Save_Status == BAD )
    return;

  BZERO ( patternlist , 128 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  /*info = fopen ( "info", "w+b");*/

  Whatever = (Uchar *) malloc (128);
  BZERO (Whatever,128);

  /* title */
  fwrite ( &Whatever[0] , 20 , 1 , out );

  /* bypass TMK.*/
  Where += 4;
  /*fprintf ( info,"bypass TMK (Where = %ld)\n",Where);*/

  /* get nbr of samples/patterns */
  NOP = in_data[Where];
  NOS = in_data[Where+1]&0x7f;
  if ( (in_data[Where+1]&0x80) == 0x80 )
    GLOBAL_DELTA = ON;
  Where += 2;
  samplesizes = (long *) malloc (NOS*sizeof(long));

  /*fprintf ( info,"NOP:%d NOS %d (Where = %ld)\n",NOP,NOS,Where);*/
  for ( i=0 ; i<NOS ; i++ )
  {
    /*sample name*/
    fwrite ( &Whatever[32] ,22 , 1 , out );

    /* siz */
    fwrite ( &in_data[Where],2,1,out);
    /* fine/vol */
    fwrite ( &in_data[Where+6],2,1,out);
    /* repstart/replen */
    fwrite ( &in_data[Where+2],4,1,out);
    
    /* whole sample size */
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    samplesizes[i] = (((in_data[Where]*256)+in_data[Where+1])*2);
    Where += 8;
  }
  Whatever[29] = 0x01;
  while (i<31)
  {
    fwrite (Whatever,30,1,out);
    i++;
  }

  /* write patlist (get also max) */
  fwrite (&NOP,1,1,out);
  Whatever[0] = 0x7f;
  fwrite (&Whatever[0],1,1,out);
  for (i=0 ; i<NOP ; i++)
  {
    patternlist[i] = in_data[Where+i]/2; /*pat list nbrs are *2 */
    if ( patternlist[i] > maxpat )
      maxpat = patternlist[i];
  }
  fwrite (patternlist,128,1,out);
  maxpat += 1; /* the first value is 0 ... */

  /*fprintf (info,"maxpat : %d(%x)\n",maxpat,maxpat);*/

  Where += NOP;

  /* write tag */
  Whatever[0] = 'M';
  Whatever[1] = Whatever[3] = '.';
  Whatever[2] = 'K';

  fwrite ( &Whatever[0] , 4 , 1 , out );
  free ( Whatever );

  /* taking care of the uneven case of patlist size */
  if ( (NOP/2)*2 != NOP )
    Where += 1;
  reftableaddy = Where;
  patdataaddy = maxpat*128 + reftableaddy; /* one ref is a short:64*2 */
  /*fprintf ( info,"reftableaddy:%x\n"
    "patdataaddy:%x\n",reftableaddy,patdataaddy);*/

  /*get siz of last 4 notes at max(patdataaddy) */
  maxlineaddy = 0;
  for ( i=reftableaddy; i<patdataaddy; i+=2 )
  {
    k = (in_data[i]*256 + in_data[i+1]);
    if ( k > maxlineaddy )
      maxlineaddy = k;
  }

  /*fprintf ( info,"maxlineaddy : %d (%x)\n",maxlineaddy,maxlineaddy);*/
  maxlineaddy += reftableaddy; /* to reach the real addy in mem/file */
  k = maxlineaddy;

  sizlastline = 0;
  for ( i=0; i<4; i++ )
  {
    if ( in_data[k] == 0x80 ){sizlastline += 1;k+=1;continue;}
    if ( (in_data[k] & 0x80) == 0x80 ){sizlastline += 2;k+=2;continue;}
    else 
      sizlastline+=3;
    k += 3;
  }

  /*fprintf (info,"sizlastline : %d\n",sizlastline);*/

  /* pat data */
  Pattern = (Uchar *) malloc (1024);
  for ( i=0 ; i<maxpat ; i+=1 )
  {
    BZERO (Pattern, 1024);
    for (j=0; j<64; j++)
    {
      k = (in_data[reftableaddy+(i*128)+(j*2)]*256)
	  +in_data[reftableaddy+(i*128)+(j*2)+1]
	+reftableaddy;
      /*fprintf (info,"--k:%lx--\n",k);*/
      for (l=0;l<4;l++)
      {
	if ( in_data[k] == 0x80 )
	{
	  /*fprintf (info,"[%2ld][%2ld][%1ld]> empty      (%lx)\n",i,j,l,k);*/
	  k += 1;
	  continue;
	}
	if ( (in_data[k] & 0xC0) == 0xC0 )
	{ /* only sfx */
	  /*fprintf (info,"[%2ld][%2ld][%1ld]> %2x,%2x      (%lx) <- sfx only\n",i,j,l,in_data[k]&0x0f,in_data[k+1],k);*/
	  Pattern[j*16+l*4+2] = in_data[k]&0x0f;
	  Pattern[j*16+l*4+3] = in_data[k+1];
	  k += 2;
	  continue;
	}
	if ( (in_data[k] & 0xC0) == 0x80 )
	{ /* note (not *2!) + smp */
	  /*fprintf (info,"[%2ld][%2ld][%1ld]> %2x,%2x      (%lx)",i,j,l,in_data[k]&0x0f,in_data[k+1],k);*/
	  Pattern[j*16+l*4]   = in_data[k+1]&0xf0;
	  Pattern[j*16+l*4]  |= poss[in_data[k]&0x7f][0];
	  Pattern[j*16+l*4+1] = poss[in_data[k]&0x7f][1];
	  Pattern[j*16+l*4+2] = in_data[k+1]<<4;
	  /*fprintf (info," ->%2x,%2x,%2x,%2x\n",Pattern[j*16+l*4],Pattern[j*16+l*4+1],Pattern[j*16+l*4+2],Pattern[j*16+l*4+3]);*/
	  k += 2;
	  continue;
	}
	else
	{
	  /*fprintf (info,"[%2ld][%2ld][%1ld]> %2x,%2x,%2x   (%lx)",i,j,l,in_data[k]&0x7f,in_data[k+1],in_data[k+2],k);*/
	  Pattern[j*16+l*4]   = (in_data[k]&0x01)<<4;
	  Pattern[j*16+l*4]  |= poss[in_data[k]/2][0];
	  Pattern[j*16+l*4+1] = poss[in_data[k]/2][1];
	  Pattern[j*16+l*4+2] = in_data[k+1];
	  Pattern[j*16+l*4+3] = in_data[k+2];
	  /*fprintf (info," ->%2x,%2x,%2x,%2x\n",Pattern[j*16+l*4],Pattern[j*16+l*4+1],Pattern[j*16+l*4+2],Pattern[j*16+l*4+3]);*/
	  k += 3;
	}
      }
      if (k+8 > Where) Where = k+8;
      /*fprintf ( info ,"\n");*/
    }
    fwrite (Pattern,1024,1,out);
  }

  free (Pattern);


  /* sample data */
  /*printf ( "Total sample size : %ld\n" , Total_Sample_Size );*/

  if ( GLOBAL_DELTA == ON )
  {
    Uchar c1,c2,c3;
    signed char *SmpDataWork;
    k = maxlineaddy+sizlastline;
    for ( i=0; i<NOS; i++ )
    {
      SmpDataWork = (signed char *) malloc ( samplesizes[i] );
      for ( j=0 ; j<samplesizes[i] ; j++ )
	SmpDataWork[j] = in_data[k+j];
      c1=SmpDataWork[0];
      for (j=1;j<samplesizes[i];j++)
      {
        c2 = SmpDataWork[j];
	c3 = c1 - c2;
        SmpDataWork[j] = c3;
        c1 = c3;
      }
      fwrite ( SmpDataWork , samplesizes[i] , 1 , out );
      free ( SmpDataWork );
      k += samplesizes[i];
    }
  }
  else
    fwrite ( &in_data[maxlineaddy+sizlastline] , Total_Sample_Size , 1 , out );

  if ( GLOBAL_DELTA == ON )
    Crap ( "        TMK       " , GOOD , BAD , out );
  else
    Crap ( "        TMK       " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*fclose ( info );*/

  printf ( "done\n" );
  free( samplesizes );
  return; /* useless ... but */
}
