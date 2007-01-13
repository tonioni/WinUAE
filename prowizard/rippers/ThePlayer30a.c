/* tests sone in ThePlayer40.c */
/* Rip_P30A() */
/* Depack_P30() */

#include "globals.h"
#include "extern.h"


void Rip_P30A ( void )
{
  /* PW_k is the number of sample */

  PW_l = ( (in_data[PW_Start_Address+16]*256*256*256) +
	   (in_data[PW_Start_Address+17]*256*256) +
	   (in_data[PW_Start_Address+18]*256) +
	   in_data[PW_Start_Address+19] );

  /* get whole sample size */
  /* starting from the highest addy and adding the sample size */
  PW_o = 0;
  for ( PW_j=0 ; PW_j<PW_k ; PW_j++ )
  {
    PW_m = ( (in_data[PW_Start_Address+20+PW_j*16]*256*256*256) +
	     (in_data[PW_Start_Address+21+PW_j*16]*256*256) +
	     (in_data[PW_Start_Address+22+PW_j*16]*256) +
	     in_data[PW_Start_Address+23+PW_j*16] );
    if ( PW_m > PW_o )
    {
      PW_o = PW_m;
      PW_n = ( (in_data[PW_Start_Address+24+PW_j*16]*256) +
	       in_data[PW_Start_Address+25+PW_j*16] );
    }
  }

  OutputSize = PW_l + PW_o + (PW_n*2) + 4;

  CONVERT = GOOD;
  Save_Rip ( "The Player 3.0A module", ThePlayer30a );
  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 2);  /* 1 should do but call it "just to be sure" :) */
}




/*
 *   The_Player_3.0.c   2003 (c) Asle / ReDoX
 *
 * The Player 3.0a to Protracker.
 * ** BETA **
 *
 * Update : 26 nov 2003
 *   - used htonl() so that use of addy is now portable on 68k archs
*/
void Depack_P30 ( void )
{
  Uchar c1,c2,c3,c4,c5;
  Uchar *Whatever;
  Uchar PatPos = 0x00;
  Uchar Nbr_Sample = 0x00;
  Uchar poss[37][2];
  Uchar sample,note,Note[2];
  Uchar Pattern_Data[128][1024];
  short Pattern_Addresses[128];
  long Track_Data_Address = 0;
  long Track_Table_Address = 0;
  long Sample_Data_Address = 0;
  long WholeSampleSize = 0;
  long SampleAddress[31];
  long SampleSize[31];
  long i=0,j,k,a,c,z;/*l*/
  long voice[4];
  long Where = PW_Start_Address;
  FILE *out;/*,*debug;*/

  if ( Save_Status == BAD )
    return;

  BZERO ( Pattern_Addresses , 128*2 );
  BZERO ( Pattern_Data , 128*1024 );
  BZERO ( SampleAddress , 31*4 );
  BZERO ( SampleSize , 31*4 );

  fillPTKtable(poss);

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );
  if ( out == NULL)
  /*  debug = fopen ( "debug", "w+b" );*/

  /* read check ID */
  Where += 4;

  /* bypass Real number of pattern */
  Where += 1;

  /* read number of pattern in pattern list */
  PatPos = (in_data[Where++]/2) - 1;

  /* read number of samples */
  Nbr_Sample = in_data[Where++];

  /* bypass empty byte */
  Where += 1;


/**********/

  /* read track data address */
  Track_Data_Address = (in_data[Where]*256*256*256)+
                       (in_data[Where+1]*256*256)+
                       (in_data[Where+2]*256)+
                        in_data[Where+3];
  Where += 4;

  /* read track table address */
  Track_Table_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;

  /* read sample data address */
  Sample_Data_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;


  /* write title */
  Whatever = (Uchar *) malloc ( 1024 );
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* sample headers stuff */
  for ( i=0 ; i<Nbr_Sample ; i++ )
  {
    /* read sample data address */
    j = (in_data[Where]*256*256*256)+
        (in_data[Where+1]*256*256)+
        (in_data[Where+2]*256)+
         in_data[Where+3];
    SampleAddress[i] = j;

    /* write sample name */
    fwrite ( Whatever , 22 , 1 , out );

    /* read sample size */
    SampleSize[i] = ((in_data[Where+4]*256)+in_data[Where+5])*2;
    WholeSampleSize += SampleSize[i];

    /* loop start */
    k = (in_data[Where+6]*256*256*256)+
        (in_data[Where+7]*256*256)+
        (in_data[Where+8]*256)+
         in_data[Where+9];

    /* writing now */
    fwrite ( &in_data[Where+4] , 2 , 1 , out );
    c1 = ((in_data[Where+12]*256)+in_data[Where+13])/74;
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &in_data[Where+15] , 1 , 1 , out );
    k -= j;
    k /= 2;
    /* use of htonl() suggested by Xigh !.*/
    z = htonl(k);
    c1 = *((Uchar *)&z+2);
    c2 = *((Uchar *)&z+3);
    fwrite ( &c1 , 1 , 1 , out );
    fwrite ( &c2 , 1 , 1 , out );
    fwrite ( &in_data[Where+10] , 2 , 1 , out );

    Where += 16;
  }

  /* go up to 31 samples */
  Whatever[29] = 0x01;
  while ( i != 31 )
  {
    fwrite ( Whatever , 30 , 1 , out );
    i += 1;
  }

  /* write size of pattern list */
  fwrite ( &PatPos , 1 , 1 , out );

  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

  /* place file pointer at the pattern list address ... should be */
  /* useless, but then ... */
  Where = PW_Start_Address + Track_Table_Address + 4;

  /* create and write pattern list .. no optimization ! */
  /* I'll optimize when I'll feel in the mood */
  for ( c1=0x00 ; c1<PatPos ; c1++ )
  {
    fwrite ( &c1 , 1 , 1 , out );
  }
  c2 = 0x00;
  while ( c1<128 )
  {
    fwrite ( &c2 , 1 , 1 , out );
    c1 += 0x01;
  }

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* reading all the track addresses .. which seem to be pattern addys ... */
  for ( i=0 ; i<PatPos ; i++ )
  {
    Pattern_Addresses[i] = (in_data[Where]*256) + in_data[Where+1] + Track_Data_Address+4;
    Where += 2;
  }


  /* rewrite the track data */
  /*printf ( "sorting and depacking tracks data ... " );*/
  for ( i=0 ; i<PatPos ; i++ )
  {
    /*fprintf (debug,"---------------------\nPattern %ld\n",i);fflush(debug);*/
    Where = PW_Start_Address + Pattern_Addresses[i];
    voice[0] = voice[1] = voice[2] = voice[3] = 0;
    for ( k=0 ; k<64 ; k++ )
    {
      for ( j=0; j<4 ; j++ )
      {
	if ( voice[j] > k )
	  continue;
	
	c1 = in_data[Where++];
	c2 = in_data[Where++];
	c3 = in_data[Where++];
	c4 = in_data[Where++];

	/*fprintf (debug,"[%2ld][%2ld][%4lx] - %2x,%2x,%2x,%2x -> ",j,voice[j],Where-4,c1,c2,c3,c4);fflush(debug);*/

        if ( c1 != 0x80 )
        {
	  sample = ((c1<<4)&0x10) | ((c2>>4)&0x0f);
	  BZERO ( Note , 2 );
	  note = c1 & 0x7f;
	  Note[0] = poss[(note/2)][0];
	  Note[1] = poss[(note/2)][1];
	  switch ( c2&0x0f )
	  {
	    case 0x08:
	      c2 -= 0x08;
	      break;
	    case 0x05:
            case 0x06:
            case 0x0A:
	      c3 = (c3 > 0x7f) ? ((c3<<4)&0xf0) : c3;
	      break;
            default:
	      break;
	  }
	  Pattern_Data[i][voice[j]*16+j*4]   = (sample&0xf0) | (Note[0]&0x0f);
	  Pattern_Data[i][voice[j]*16+j*4+1] = Note[1];
	  Pattern_Data[i][voice[j]*16+j*4+2] = c2;
	  Pattern_Data[i][voice[j]*16+j*4+3] = c3;

	  /*fprintf ( debug, "%2x,%2x,%2x,%2x",Pattern_Data[i][voice[j]*16+j*4],Pattern_Data[i][voice[j]*16+j*4+1],Pattern_Data[i][voice[j]*16+j*4+2],Pattern_Data[i][voice[j]*16+j*4+3]);fflush(debug);*/

          if ( (c4 > 0x00) && (c4 <0x80) )
	  {
            voice[j] += c4;
	    /*fprintf ( debug, "  <-- %d empty lines",c4 );fflush(debug);*/
	  }
	  /*fprintf ( debug, "\n" );fflush(debug);*/
          voice[j] += 1; 
	} /* end of case 0x80 for first byte */

	else /* repeat some lines */
        {
	  a = Where;

	  c5 = c2;
	  Where -= (((c3&0x7f)*256)+ (c4*4));
	  /*fprintf ( debug , "\n![%2ld] - go back %d bytes and read %d notes (at %lx)\n" , i , (((c3&0x7f)*256)+ (c4*4)),c5 , a-4 );fflush(debug);*/
          for ( c=0 ; c<=c5 ; c++ )
          {
	    /*fprintf ( debug , "%ld," , k );*/
	    c1 = in_data[Where++];
	    c2 = in_data[Where++];
	    c3 = in_data[Where++];
	    c4 = in_data[Where++];
	    /*fprintf (debug,"[%2ld][%2ld][%4lx] - %2x,%2x,%2x,%2x -> ",j,voice[j],Where-4,c1,c2,c3,c4);fflush(debug);*/
            
	    sample = ((c1<<4)&0x10) | ((c2>>4)&0x0f);
	    BZERO ( Note , 2 );
	    note = c1 & 0x7f;
	    Note[0] = poss[(note/2)][0];
	    Note[1] = poss[(note/2)][1];
	    switch ( c2&0x0f )
	    {
	      case 0x08:
		c2 -= 0x08;
		break;
              case 0x05:
              case 0x06:
              case 0x0A:
		c3 = (c3 > 0x7f) ? ((c3<<4)&0xf0) : c3;
		break;
              default:
		break;
	    }
	    Pattern_Data[i][voice[j]*16+j*4]   = (sample&0xf0) | (Note[0]&0x0f);
	    Pattern_Data[i][voice[j]*16+j*4+1] = Note[1];
	    Pattern_Data[i][voice[j]*16+j*4+2] = c2;
	    Pattern_Data[i][voice[j]*16+j*4+3] = c3;

	    /*fprintf ( debug, "%2x,%2x,%2x,%2x",Pattern_Data[i][voice[j]*16+j*4],Pattern_Data[i][voice[j]*16+j*4+1],Pattern_Data[i][voice[j]*16+j*4+2],Pattern_Data[i][voice[j]*16+j*4+3]);fflush(debug);*/
	    if ( (c4 > 0x00) && (c4 <0x80) )
	    {
	      voice[j] += c4;
	      c += c4; /* empty lines are counted ! */
	      /*fprintf ( debug, "  <-- %d empty lines",c4 );fflush(debug);*/
	    }
	    voice[j] += 1;
	    /*fprintf ( debug, "\n" );fflush(debug);*/
	  }

	  voice[j] -= 1;
	  Where = a;
	  /*fprintf ( debug , "\n!## back to %lx\n" , a );fflush(debug);*/
        }
      }
    }
  }
  /*  printf ( "ok\n" );*/



  /* write pattern data */
  /*printf ( "writing pattern data ... " );*/
  /*fflush ( stdout );*/
  for ( i=0 ; i<PatPos ; i++ )
  {
    fwrite ( Pattern_Data[i] , 1024 , 1 , out );
  }
  free ( Whatever );
  /*printf ( "ok\n" );*/


  /* read and write sample data */
  /*printf ( "writing sample data ... " );*/
  for ( i=0 ; i<Nbr_Sample ; i++ )
  {
    Where = PW_Start_Address + SampleAddress[i]+Sample_Data_Address;
    fwrite ( &in_data[Where] , SampleSize[i] , 1 , out );
  }
  /*printf ( "ok\n" );*/

  Crap ( " The Player 3.0A  " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );
  /*  fclose ( debug );*/

  printf ( "done\n" );
  return; /* useless ... but */
}
