/*
 *   Digital_Illusion.c   1997 (c) Asle / ReDoX
 *
 * Converts DI packed MODs back to PTK MODs
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


void Depack_DI ( void )
{
  Uchar Note,Smp,Fx,FxVal;
  Uchar poss[37][2];
  Uchar *Whatever;
  long i=0,k=0;
  Ushort Pattern_Addresses_Table[128];
  long Add_Pattern_Table=0;
  long Add_Pattern_Data=0;
  long Add_Sample_Data=0;
  long Total_Sample_Size=0;
  long Where = PW_Start_Address;
  FILE *out;

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  BZERO ( Pattern_Addresses_Table , 128*2 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  k = (in_data[Where]*256)+in_data[Where+1];
  Where += 2;
  /*printf ( "Number of sample : %d\n" , k );*/

  Add_Pattern_Table = (in_data[Where+1]*256*256)+
                      (in_data[Where+2]*256)+
                       in_data[Where+3];
  Where += 4;
  /*printf ( "Pattern table address : %ld\n" , Add_Pattern_Table );*/

  Add_Pattern_Data = (in_data[Where+1]*256*256)+
                     (in_data[Where+2]*256)+
                      in_data[Where+3];
  Where += 4;
  /*printf ( "Pattern data address : %ld\n" , Add_Pattern_Data );*/

  Add_Sample_Data = (in_data[Where+1]*256*256)+
                    (in_data[Where+2]*256)+
                     in_data[Where+3];
  Where += 4;
  /*printf ( "Sample data address : %ld\n" , Add_Sample_Data );*/


  for ( i=0 ; i<k ; i++ )
  {
    fwrite ( Whatever , 22 , 1 , out );

    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 8 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  Whatever[29] = 0x01;
  for ( i=k ; i<31 ; i++ )
    fwrite ( Whatever , 30 , 1 , out );

  k = Where;

  Where = PW_Start_Address + Add_Pattern_Table;
  i=0;
  do
  {
    Whatever[200] = in_data[Where++];
    Whatever[i]=Whatever[200];
    i+=1;
  }while ( Whatever[200] != 0xff );
  Whatever[i-1] = 0x00;
  Whatever[257] = i-1;
  fwrite ( &Whatever[257] , 1 , 1 , out );

  Whatever[256] = 0x7f;
  fwrite ( &Whatever[256] , 1 , 1 , out );

  Whatever[256] = 0;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( Whatever[i] > Whatever[256] )
      Whatever[256] = Whatever[i];
  }
  fwrite ( Whatever , 128 , 1 , out );

  /*printf ( "Number of pattern : %d\n" , Whatever[257] );*/
  /*printf ( "Highest pattern number : %d\n" , Whatever[256] );*/

  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );


  Where = k;
  for ( i=0 ; i<=Whatever[256] ; i++ )
  {
    Pattern_Addresses_Table[i] = (in_data[Where]*256)+in_data[Where+1];
    Where += 2;
  }

  for ( i=0 ; i<=Whatever[256] ; i++ )
  {
    Where = PW_Start_Address + Pattern_Addresses_Table[i];
    for ( k=0 ; k<256 ; k++ )  /* 256 = 4(voices) * 64(rows) */
    {
      Whatever[0] = Whatever[1] = Whatever[2] = Whatever[3] = 0x00;
      Whatever[10] = in_data[Where++];
      if ( (Whatever[10] & 0x80) == 0 )
      {
        Whatever[11] = in_data[Where++];
	Note = ( ((Whatever[10] << 4) & 0x30) | ( ( Whatever[11]>>4 ) & 0x0f ) );
	Whatever[0] = poss[Note][0];
	Whatever[1] = poss[Note][1];
	Smp = (Whatever[10] >> 2) & 0x1f;
	Whatever[0] |= (Smp & 0xf0);
	Whatever[2] = (Smp << 4) & 0xf0;
	Fx = Whatever[11] & 0x0f;
	Whatever[2] |= Fx;
	FxVal = 0x00;
	Whatever[3] = FxVal;
	fwrite ( Whatever , 4 , 1 , out );
	continue;
      }
      if ( Whatever[10] == 0xff )
      {
        Whatever[0] = Whatever[1] = Whatever[2] = Whatever[3] = 0x00;
	fwrite ( Whatever , 4 , 1 , out );
	continue;
      }
      Whatever[11] = in_data[Where++];
      Whatever[12] = in_data[Where++];
      Note = ( ((Whatever[10] << 4) & 0x30) | ( (Whatever[11] >> 4) & 0x0f ) );
      Whatever[0] = poss[Note][0];
      Whatever[1] = poss[Note][1];
      Smp = (Whatever[10] >> 2) & 0x1f;
      Whatever[0] |= (Smp & 0xf0);
      Whatever[2] = (Smp << 4) & 0xf0;
      Fx = Whatever[11] & 0x0f;
      Whatever[2] |= Fx;
      FxVal = Whatever[12];
      Whatever[3] = FxVal;
      fwrite ( Whatever , 4 , 1 , out );
    }
  }
  free ( Whatever );


  Where = PW_Start_Address + Add_Sample_Data;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  Crap ( " Digital Illusion " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */

}
