/*
 *  StarTrekker _Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk StarTrekker packed MODs
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


void Depack_STARPACK ( void )
{
  Uchar c1=0x00,c2=0x00;
  Uchar Pat_Pos;
  Uchar *Whatever;
  Uchar *Pattern;
  long i=0,j=0,k=0;
  long Total_Sample_Size=0;
  long Pats_Address[128];
  long Read_Pats_Address[128];
  long SampleDataAddress=0;
  long Where = PW_Start_Address;
  long MaxPatAddy=0;
  FILE /**in,*/*out;

  if ( Save_Status == BAD )
    return;

  BZERO ( Pats_Address , 128*4 );

  /*in = fopen ( OutName_final , "r+b" );*/ /* +b is safe bcoz OutName's just been saved */
  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;


  Whatever = (Uchar *) malloc (1024);
  BZERO (Whatever, 1024);

  /* read and write title */
  fwrite (&in_data[Where], 20, 1, out );
  Where += 20;

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    fwrite ( &Whatever[0], 22, 1, out );
    /*sample name*/

    Total_Sample_Size += (((in_data[Where+i*8]*256)+in_data[Where+1+i*8])*2);
    fwrite ( &in_data[Where+i*8], 8, 1, out );
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/
  Where = PW_Start_Address + 268;

  /* read size of pattern table */
  Pat_Pos = ((in_data[Where]*256)+in_data[Where+1])/4;
  Where += 4;
  fwrite ( &Pat_Pos, 1, 1, out );
  /*printf ( "Size of pattern table : %d\n" , Pat_Pos );*/
  /* bypass $0000 unknown bytes */

/***********/

  for ( i=0 ; i<128 ; i++ )
  {
    Pats_Address[i] = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3];
    Where+=4;
    if ( Pats_Address[i] > MaxPatAddy )
      MaxPatAddy = Pats_Address[i];
  }


  /* write noisetracker byte */
  c1 = 0x7f;
  fwrite ( &c1 , 1 , 1 , out );

/***********/


  /* read sample data address */
  Where = PW_Start_Address + 0x310;
  SampleDataAddress = (in_data[Where]*256*256*256)+(in_data[Where+1]*256*256)+(in_data[Where+2]*256)+in_data[Where+3]+0x314;

  /* pattern data */
  Where += 4;
  /*PatMax += 1;*/

  c1=0; /* will count patterns */
  k=0; /* current note number */
  Pattern = (Uchar *) malloc (65536);
  BZERO (Pattern, 65536);
  i=0;
  for ( j=0 ; j<(MaxPatAddy+0x400) ; j+=4 )
  {
    if ( (i%1024) == 0 )
    {
      if ( j>MaxPatAddy )
	break;
      Read_Pats_Address[c1] = j;
      c1 += 0x01;
    }
    if (in_data[Where+j] == 0x80 )
    {
      j -= 3;
      i += 4;
      continue;
    }

    c2 = ((in_data[Where+j]) | ((in_data[Where+j+2]>>4)&0x0f)) / 4;

    Pattern[i]   = in_data[Where+j] & 0x0f;
    Pattern[i]  |= (c2&0xf0);
    Pattern[i+1] = in_data[Where+j+1];
    Pattern[i+2] = in_data[Where+j+2] & 0x0f;
    Pattern[i+2]|= ((c2<<4)&0xf0);
    Pattern[i+3] = in_data[Where+j+3];

    i += 4;
  }
  /* Where should be now on the sample data addy */

  /* write pattern list */
  /* write pattern table */
  BZERO ( Whatever, 128 );
  for ( c2=0; c2<128 ; c2+=0x01 )
    for ( i=0 ; i<128 ; i++ )
      if ( Pats_Address[c2] == Read_Pats_Address[i])
      {
	Whatever[c2] = (Uchar) i;
	break;
      }
  fwrite ( &Whatever[0], 128, 1, out );

  /* write ptk's ID */
  Whatever[0] = 'M';Whatever[1] = '.';Whatever[2] = 'K';
  fwrite ( &Whatever[0] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  fwrite ( &Whatever[2] , 1 , 1 , out );
  fwrite ( &Whatever[1] , 1 , 1 , out );
  free (Whatever);

  /* write pattern data */
  /* c1 is still the number of patterns stored */
  fwrite ( Pattern, c1*1024, 1, out );

  /* sample data */
  Where = PW_Start_Address + SampleDataAddress;

  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );


  Crap ( " StarTrekker Pack " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , "-[StarTrekker pack]-" );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "  -[2 Protracker]-  " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
