/*
 *   Polka.c   2003 (c) Asle
 *
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_Polka ( void )
{
  Uchar poss[37][2];
  Uchar c1=0x00,c2=0x00;
  Uchar Max=0x00;
  long WholeSampleSize=0;
  long i=0,j;
  long Where = PW_Start_Address;
  FILE *out;
  unsigned char Whatever[4];

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* takes care of header */
  fwrite ( &in_data[Where], 20, 1, out );
  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( &in_data[Where+20+i*30], 18, 1, out );
    c1=0x00;
    fwrite ( &c1, 1, 1, out );fwrite ( &c1, 1, 1, out );
    fwrite ( &c1, 1, 1, out );fwrite ( &c1, 1, 1, out );
    fwrite ( &in_data[Where+42+i*30], 8, 1, out );
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write size of pattern list+ntk byte + pattern list */
  fwrite ( &in_data[Where+0x3b6] , 130 , 1 , out );

  /* write ID */
  c1 = 'M';
  c2 = '.';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );
  c1 = 'K';
  fwrite ( &c1 , 1 , 1 , out );
  fwrite ( &c2 , 1 , 1 , out );

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i+0x3b8] > Max )
      Max = in_data[Where+i+0x3b8];
  }
  Max += 1;
  /*printf ( "\nNumber of pattern : %ld\n" , j );*/

  /* pattern data */
  Where = PW_Start_Address + 0x43c;
  for ( i=0 ; i<Max ; i++ )
  {
    for ( j=0 ; j<256 ; j++ )
    {
      Whatever[0] = in_data[Where+1] & 0xf0;
      Whatever[2] = (in_data[Where+1] & 0x0f)<<4;
      Whatever[2] |= in_data[Where+2];
      Whatever[3] = in_data[Where+3];
      Whatever[0] |= poss[(in_data[Where])/2][0];
      Whatever[1] = poss[(in_data[Where]/2)][1];
      fwrite ( Whatever , 4 , 1 , out );
      Where += 4;
    }
  }

  /* sample data */
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );


  /* crap */
  Crap ( "   Polka Packer   " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[Power Music to]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[Protracker]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
