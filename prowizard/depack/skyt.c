/*
 *   Skyt_Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk SKYT packed MODs
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 Nov 1999 : Update
 *   - Speed & Size optimizings
 * 19 Apr 2000 : Update
 *   - address of samples data bug correction
 *     (thx to Thomas Neumann)
 * 29 Nov 2003 : Update
 *   - another bug removed :(.
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_SKYT ( void )
{
  Uchar Pat_Pos;
  Uchar *Whatever;
  Uchar poss[37][2];
  long i=0,j=0,k=0;
  long Total_Sample_Size=0;
  long Track_Values[128][4];
  long Track_Address;
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  long Highest_Track = 0;
  FILE *out;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  BZERO ( Track_Values , 128*16 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  /* title */
  fwrite ( Whatever , 20 , 1 , out );

  /* read and write sample descriptions */
  for ( i=0 ; i<31 ; i++ )
  {
    /*sample name*/
    fwrite ( Whatever , 22 , 1 , out );
    Total_Sample_Size += (((in_data[Where]*256)+in_data[Where+1])*2);
    /* write Size,Fine,Vol & Loop start */
    fwrite ( &in_data[Where] , 6 , 1 , out );
    /* loop size */
    Whatever[32] = in_data[Where+7];
    if ( (in_data[Where+6] == 0x00) && (in_data[Where+7] == 0x00) )
      Whatever[32] = 0x01;
    fwrite ( &in_data[Where+6] , 1 , 1 , out );
    fwrite ( &Whatever[32] , 1 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , Total_Sample_Size );*/

  /* bypass 8 empty bytes and bypass "SKYT" ID*/
  Where = PW_Start_Address + 260;

  /* pattern table lenght */
  Pat_Pos = in_data[Where]+1;
  Where += 1;
  fwrite ( &Pat_Pos , 1 , 1 , out );
  /*printf ( "Size of pattern list : %d\n" , Pat_Pos );*/

  /* write NoiseTracker byte */
  Whatever[32] = 0x7f;
  fwrite ( &Whatever[32] , 1 , 1 , out );

  /* read track numbers ... and deduce pattern list */
  for ( i=0 ; i<Pat_Pos ; i++ )
  {
    for ( j=0 ; j<4 ; j++ )
    {
      Track_Values[i][j] = in_data[Where+1];
      if ( Track_Values[i][j] > Highest_Track )
	Highest_Track = Track_Values[i][j];
      Where += 2;
    }
  }
  /*printf ( "\nHighest track : %ld\n", Highest_Track );*/

  /* write pseudo pattern list */
  for ( Whatever[0]=0x00 ; Whatever[0]<Pat_Pos ; Whatever[0]+=0x01 )
  {
    fwrite ( &Whatever[0] , 1 , 1 , out );
  }
  Whatever[1] = 0x00;
  while ( Whatever[0] != 128 )
  {
    fwrite ( &Whatever[1] , 1 , 1 , out );
    Whatever[0] += 0x01;
  }

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* bypass $00 unknown byte */
  /*Where += 1;*/

  /* get track address */
  Where = PW_Start_Address + 261 + (Pat_Pos*8) + 1;
  Track_Address = Where;
  /*printf ("Track_Address : %ld\n",Track_Address);*/


  /* track data */
  for ( i=0 ; i<Pat_Pos ; i++ )
  {
    BZERO ( Whatever , 1024 );
    for ( j=0 ; j<4 ; j++ )
    {
      /*Where = PW_Start_Address + Track_Address + (Track_Values[i][j]-1)*256;*/
      Where = Track_Address + (Track_Values[i][j]-1)*256;
      for ( k=0 ; k<64 ; k++ )
      {
        Whatever[k*16+j*4] = in_data[Where+1]&0xf0;
        Whatever[k*16+j*4] |= poss[in_data[Where]][0];
        Whatever[k*16+j*4+1] = poss[in_data[Where]][1];
        Whatever[k*16+j*4+2] = (in_data[Where+1]<<4)&0xf0;
        Whatever[k*16+j*4+2] |= in_data[Where+2];
        Whatever[k*16+j*4+3] = in_data[Where+3];
        Where += 4;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
    /*printf ( "+" );*/
  }
  free ( Whatever );
  /*printf ( "\n" );*/

  /* sample data */
  Where = Track_Address + Highest_Track*256;
  fwrite ( &in_data[Where] , Total_Sample_Size , 1 , out );

  /* crap */
  Crap ( "   SKYT Packer    " , BAD , BAD , out );

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
