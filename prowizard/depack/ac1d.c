/*
 *   ac1d.c   1996-1997 (c) Asle / ReDoX
 *
 * Converts AC1D packed MODs back to PTK MODs
 * thanks to Gryzor and his ProWizard tool ! ... without it, this prog
 * would not exist !!!
 *
 * Last update: 30/11/99
 *   - removed open() (and other fread()s and the like)
 *   - general Speed & Size Optmizings
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif


#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_AC1D ( void )
{
  Uchar NO_NOTE=0xff;
  Uchar c1,c2,c3,c4;
  Uchar *Whatever;
  Uchar Nbr_Pat;
  Uchar poss[37][2];
  Uchar Note,Smp,Fx,FxVal;
  long Sample_Data_Address;
  long WholeSampleSize=0;
  long Pattern_Addresses[128];
  long Pattern_Sizes[128];
  long siztrack1,siztrack2,siztrack3;
  long i,j,k;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

  memset (Pattern_Addresses , 0 , 128*4);
  BZERO (Pattern_Sizes , 128*4);

    
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

  /* bypass ID */
  Where += 4;

  Sample_Data_Address = (in_data[Where]*256*256*256)+
                        (in_data[Where+1]*256*256)+
                        (in_data[Where+2]*256)+
                         in_data[Where+3];
  Where += 4;
  /*printf ( "adress of sample datas : %ld\n" , Sample_Data_Address );*/

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( Whatever , 22 , 1 , out );
    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* pattern addresses */
  for ( Nbr_Pat=0 ; Nbr_Pat<128 ; Nbr_Pat++ )
  {
    Pattern_Addresses[Nbr_Pat] = (in_data[Where]*256*256*256)+
                                 (in_data[Where+1]*256*256)+
                                 (in_data[Where+2]*256)+
                                  in_data[Where+3];
    Where += 4;
    if ( Pattern_Addresses[Nbr_Pat] == 0 )
      break;
  }
  Nbr_Pat -= 1;
  /*printf ( "Number of pattern saved : %d\n" , Nbr_Pat );*/

  for ( i=0 ; i<(Nbr_Pat-1) ; i++ )
  {
    Pattern_Sizes[i] = Pattern_Addresses[i+1]-Pattern_Addresses[i];
  }


  /* write number of pattern pos */
  /* write "noisetracker" byte */
  fwrite ( &in_data[PW_Start_Address] , 2 , 1 , out );

  /* go to pattern table .. */
  Where = PW_Start_Address + 0x300;

  /* pattern table */
  fwrite ( &in_data[Where] , 128 , 1, out );
  Where += 128;

  /* write ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  /* pattern data */
  for ( i=0 ; i<Nbr_Pat ; i++ )
  {
    Where = PW_Start_Address + Pattern_Addresses[i];
    siztrack1 = (in_data[Where]*256*256*256)+
                (in_data[Where+1]*256*256)+
                (in_data[Where+2]*256)+
                 in_data[Where+3];
    Where += 4;
    siztrack2 = (in_data[Where]*256*256*256)+
                (in_data[Where+1]*256*256)+
                (in_data[Where+2]*256)+
                 in_data[Where+3];
    Where += 4;
    siztrack3 = (in_data[Where]*256*256*256)+
                (in_data[Where+1]*256*256)+
                (in_data[Where+2]*256)+
                 in_data[Where+3];
    Where += 4;
    BZERO ( Whatever , 1024 );
    for ( k=0 ; k<4 ; k++ )
    {
      for ( j=0 ; j<64 ; j++ )
      {
        Note = Smp = Fx = FxVal = 0x00;
        c1 = in_data[Where++];
        if ( ( c1 & 0x80 ) == 0x80 )
        {
          c4 = c1 & 0x7f;
          j += (c4 - 1);
          continue;
        }
        c2 = in_data[Where++];
        Smp = ( (c1&0xc0) >> 2 );
        Smp |= (( c2 >> 4 ) & 0x0f);
        Note = c1 & 0x3f;
        if ( Note == 0x3f )
          Note = NO_NOTE;
        else if ( Note != 0x00 )
          Note -= 0x0b;
        if ( Note == 0x00 )
          Note += 0x01;
        Whatever[j*16+k*4] = Smp&0xf0;
        if ( Note != NO_NOTE )
        {
          Whatever[j*16+k*4] |= poss[Note][0];
          Whatever[j*16+k*4+1] = poss[Note][1];
        }
        if ( (c2 & 0x0f) == 0x07 )
        {
          Fx = 0x00;
          FxVal = 0x00;
          Whatever[j*16+k*4+2] = (Smp << 4)&0xf0;
          continue;
        }
        c3 = in_data[Where++];
        Fx = c2 & 0x0f;
        FxVal = c3;
        Whatever[j*16+k*4+2] = ((Smp<<4)&0xf0);
        Whatever[j*16+k*4+2] |= Fx;
        Whatever[j*16+k*4+3] = FxVal;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/

  /* sample data */
  Where = PW_Start_Address + Sample_Data_Address;
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap ... */
  Crap ( "   AC1D Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
