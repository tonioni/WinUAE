/*
 *   Promizer_0.1_Packer.c   1997 (c) Asle / ReDoX
 *
 * Converts back to ptk Promizer 0.1 packed MODs
 *
 * ---updates : 2000, the 19th of april
 *  - Small bug correction (pointed out by Thoman Neumann)
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_PM01 ( void )
{
  Uchar c1=0x00,c2=0x00,c3=0x00;
  Uchar Pat_Pos;
  Uchar poss[37][2];
  Uchar *Whatever;
  Uchar *PatternData;
  Uchar Smp_Fine_Table[31];
  Uchar Old_Smp_Nbr[4];
  long i=0,j=0,k=0,l=0;
  long WholeSampleSize=0;
  long Pattern_Address[128];
  long Where = PW_Start_Address;
  FILE *out;

#ifdef DOS
  #include "..\include\tuning.h"
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/tuning.h"
  #include "../include/ptktable.h"
#endif

  if ( Save_Status == BAD )
    return;

  BZERO ( Pattern_Address , 128*4 );
  BZERO ( Smp_Fine_Table , 31 );
  BZERO ( Old_Smp_Nbr , 4 );

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

    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    fwrite ( &in_data[Where] , 2 , 1 , out );
    c1 = in_data[Where+2]; /* finetune */
    Smp_Fine_Table[i] = c1;
    fwrite ( &c1 , 1 , 1 , out );

    fwrite ( &in_data[Where+3] , 3 , 1 , out );
    Whatever[32] = in_data[Where+7];
    if ( (in_data[Where+6] == 0x00) && (Whatever[32] == 0x00) )
      Whatever[32] = 0x01;
    fwrite ( &in_data[Where+6] , 1 , 1 , out );
    fwrite ( &Whatever[32] , 1 , 1 , out );
    Where += 8;
  }
  /*printf ( "Whole sample size : %ld\n" , WholeSampleSize );*/

  /* pattern table lenght */
  Pat_Pos = ((in_data[Where]*256)+in_data[Where+1])/4;
  Where += 2;
  fwrite ( &Pat_Pos , 1 , 1 , out );
  /*printf ( "Size of pattern list : %d\n" , Pat_Pos );*/

  /* write NoiseTracker byte */
  Whatever[0] = 0x7F;
  fwrite ( Whatever , 1 , 1 , out );

  /* read pattern address list */
  for ( i=0 ; i<128 ; i++ )
  {
    Pattern_Address[i] = (in_data[Where]*256*256*256)+
                         (in_data[Where+1]*256*256)+
                         (in_data[Where+2]*256)+
                          in_data[Where+3];
    Where += 4;
  }

  /* deduce pattern list and write it */
  for ( i=0 ; i<128 ; i++ )
  {
    Whatever[i] = Pattern_Address[i]/1024;
  }
  fwrite ( Whatever , 128 , 1 , out );

  /* write ptk's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* get pattern data size */
  j = (in_data[Where]*256*256*256)+
      (in_data[Where+1]*256*256)+
      (in_data[Where+2]*256)+
       in_data[Where+3];
  Where += 4;
  /*printf ( "Size of the pattern data : %ld\n" , j );*/

  /* read and XOR pattern data */
  free ( Whatever );
  Whatever = (Uchar *) malloc ( j );
  PatternData = (Uchar *) malloc ( j );
  for ( k=0 ; k<j ; k++ )
  {
    if ( k%4 == 3 )
    {
      PatternData[k] = ((240 - (in_data[Where+k]&0xf0))+(in_data[Where+k]&0x0f));
      continue;
    }
    PatternData[k] = 255 - in_data[Where+k];
  }

  /* all right, now, let's take care of these 'finetuned' value ... pfff */
  Old_Smp_Nbr[0]=Old_Smp_Nbr[1]=Old_Smp_Nbr[2]=Old_Smp_Nbr[3]=0x1f;
  BZERO ( Whatever , j );
  for ( i=0 ; i<j/4 ; i++ )
  {
    c1 = PatternData[i*4]&0x0f;
    c2 = PatternData[i*4+1];
    k = (c1*256)+c2;
    c3 = (PatternData[i*4]&0xf0) | ((PatternData[i*4+2]>>4)&0x0f);
    if ( c3 == 0 )
      c3 = Old_Smp_Nbr[i%4];
    else
      Old_Smp_Nbr[i%4] = c3;
    if ( (k != 0) && (Smp_Fine_Table[c3-1] != 0x00) )
    {
/*fprintf ( info , "! (at %ld)(smp:%x)(pitch:%ld)\n" , (i*4)+382 , c3 , k );*/
      for ( l=0 ; l<36 ; l++ )
      {
        if ( k == Tuning[Smp_Fine_Table[c3-1]][l] )
        {
          Whatever[i*4]   = poss[l+1][0];
          Whatever[i*4+1] = poss[l+1][1];
        }
      }
    }
    else
    {
      Whatever[i*4] = PatternData[i*4]&0x0f;
      Whatever[i*4+1] = PatternData[i*4+1];
    }
    Whatever[i*4]  |= (PatternData[i*4]&0xf0);
    Whatever[i*4+2] = PatternData[i*4+2];
    Whatever[i*4+3] = PatternData[i*4+3];
  }
  fwrite ( Whatever , j , 1 , out );
  free ( Whatever );
  free ( PatternData );

  /* sample data */
  Where += j;
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  Crap ( "   Promizer 0.1   " , BAD , BAD , out );

  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
