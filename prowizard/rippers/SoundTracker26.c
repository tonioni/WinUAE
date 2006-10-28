/* testSTK26() */
/* Rip_STK26() */
/* Depack_STK26() */


#include "globals.h"
#include "extern.h"



/* Soundtracker 2.6 & IceTracker 1.0 */
short testSTK26 ( void )
{
  /* test 1 */
  if ( PW_i < 1464 )
  {
    return BAD;
  }

  /* test 2 */
  PW_Start_Address = PW_i-1464;
  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+45+30*PW_j] > 0x40 )
    {
/*      printf ( "#1\n" );*/
      return BAD;
    }
    if ( in_data[PW_Start_Address+44+30*PW_j] > 0x0F )
    {
/*      printf ( "#2\n" );*/
      return BAD;
    }
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+
                            in_data[PW_Start_Address+43+PW_j*30])*2);
  }

  /* PW_WholeSampleSize is the whole sample size :) */
  return GOOD;
}




void Rip_STK26 ( void )
{
  /* PW_WholeSampleSize is the whole sample siz */
  OutputSize = (in_data[PW_Start_Address+951]*256) + 1468 + PW_WholeSampleSize;

  CONVERT = GOOD;
  if ( in_data[PW_Start_Address+1464] == 'M' )
  {
    Save_Rip ( "Sountracker 2.6 module", STK26 );
  }
  else
  {
    Save_Rip ( "IceTracker 1.0 modul", IceTracker );
  }

  
  if ( Save_Status == GOOD )
    PW_i += (OutputSize - 1470);  /* 1464 should do but call it "just to be sure" :) */
}



/*
 *   STK26.c   1999 (c) Asle / ReDoX
 *
 *
 * handles Soundtracker 2.6 and IceTracker 1.0 file
 * convert them back to PTK
 *
 * Update: 19/04/00
 *  - forced 7F value to NTK byte
 * Update: 29/05/02
 *  - above update was bugged ... great.
*/
void Depack_STK26 ( void )
{
  Uchar *Whatever;
  long WholeSampleSize=0;
  long Where=PW_Start_Address;
  long i=0,j,k;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );


  /* read and write whole header */
  fwrite ( &in_data[Where] , 952 , 1 , out );

  /* get whole sample size */
  for ( i=0 ; i<31 ; i++ )
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  /*  printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* generate patlist */
  Whatever = (Uchar *) malloc (1536);
  BZERO ( Whatever , 1536 );
  Whatever[1024] = in_data[Where+950];
  for ( i=0 ; i<Whatever[1024] ; i++,Whatever[256] += 0x01 )
    Whatever[i] = Whatever[256];
  fwrite ( Whatever , 128 , 1 , out );

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* NTK byte */
  Whatever[0] = 0x7f;
  fseek ( out , 951 , 0 );
  fwrite ( &Whatever[0] , 1 , 1 , out );
  fseek ( out , 0 , 2 );

  /* I dont care highest track number and go on blindly to convert shit ! */
  Where += 952;
  for ( i=0 ; i<Whatever[1024] ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      for ( k=0 ; k<64 ; k++ )
      {
        Whatever[k*16+j*4]   = in_data[Where+(in_data[Where+i*4+j])*256+k*4+516];
        Whatever[k*16+j*4+1] = in_data[Where+(in_data[Where+i*4+j])*256+k*4+517];
        Whatever[k*16+j*4+2] = in_data[Where+(in_data[Where+i*4+j])*256+k*4+518];
        Whatever[k*16+j*4+3] = in_data[Where+(in_data[Where+i*4+j])*256+k*4+519];
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
  }
  free ( Whatever );

  /* sample data */
  /*Where = PW_Start_Address + 516 + 952 + Max*256;*/
  Where = 1468 + in_data[951]*256;
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */

  if ( in_data[PW_Start_Address+1464] == 'M' )
    Crap ( " SoundTracker 2.6 " , BAD , BAD , out );
  else
    Crap ( "  IceTracker 1.0  " , BAD , BAD , out );

  fclose ( out );

  /*  printf ( "done\n" );*/
  return; /* useless ... but */
}

