/*
 *   STIM_Packer.c   1998 (c) Sylvain "Asle" Chipaux
 *
 * STIM Packer to Protracker.
 ********************************************************
 * 13 april 1999 : Update
 *   - no more open() of input file ... so no more fread() !.
 *     It speeds-up the process quite a bit :).
 * 28 Nov 1999 : Update
 *   - Speed & Size optimizings
*/

#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif

void Depack_STIM ( void )
{
  Uchar *Whatever;
  Uchar c1=0x00,c2=0x00,c3=0x00,c4=0x00;
  Uchar poss[36][2];
  Uchar Max=0x00;
  Uchar Note,Smp,Fx,FxVal;
  short TracksAdd[4];
  long i=0,j=0,k=0;
  long WholeSampleSize=0;
  long SmpDescAdd=0;
  long PatAdds[64];
  long SmpDataAdds[31];
  long SmpSizes[31];
  long Where=PW_Start_Address;   /* main pointer to prevent fread() */
  FILE *out;

  if ( Save_Status == BAD )
    return;

#ifdef DOS
  #include "..\include\ptktable.h"
#endif

#ifdef UNIX
  #include "../include/ptktable.h"
#endif

  BZERO ( PatAdds , 64*4 );
  BZERO ( SmpDataAdds , 31*4 );
  BZERO ( SmpSizes , 31*4 );

  sprintf ( Depacked_OutName , "%ld.mod" , Cpt_Filename-1 );
  out = mr_fopen ( Depacked_OutName , "w+b" );
  if (!out)
      return;

  /* write title */
  Whatever = (Uchar *) malloc (1024);
  BZERO ( Whatever , 1024 );
  fwrite ( Whatever , 20 , 1 , out );

  /* bypass ID */
  Where += 4;

  /* read $ of sample description */
  SmpDescAdd = (in_data[Where]*256*256*256)+
               (in_data[Where+1]*256*256)+
               (in_data[Where+2]*256)+
                in_data[Where+3];
  /* "Where" isn't "+=4" coz it's assigned below */
  /*printf ( "SmpDescAdd : %ld\n" , SmpDescAdd );*/

  /* convert and write header */
  for ( i=0 ; i<31 ; i++ )
  {
    Where = PW_Start_Address + SmpDescAdd + i*4;
    SmpDataAdds[i]=(in_data[Where]*256*256*256)+
                   (in_data[Where+1]*256*256)+
                   (in_data[Where+2]*256)+
                    in_data[Where+3];
    SmpDataAdds[i] += SmpDescAdd;
    Where = PW_Start_Address + SmpDataAdds[i];
    SmpDataAdds[i] += 8;

    /* write sample name */
    fwrite ( Whatever , 22 , 1 , out );

    /* sample size */
    SmpSizes[i] = (((in_data[Where]*256)+in_data[Where+1])*2);
    WholeSampleSize += (((in_data[Where]*256)+in_data[Where+1])*2);
    /* size,fine,vol,loops */
    fwrite ( &in_data[Where] , 8 , 1 , out );

    /* no "Where += 8" coz it's reassigned inside and after loop */
  }

  /* size of the pattern list */
  Where = PW_Start_Address + 19;
  fwrite ( &in_data[Where++] , 1 , 1 , out );
  Whatever[0] = 0x7f;
  fwrite ( Whatever , 1 , 1 , out );

  /* pattern table */
  Where += 1;
  Max = in_data[Where++];
  fwrite ( &in_data[Where] , 128 , 1 , out );
  Where += 128;

  /*printf ( "number of pattern : %d\n" , Max );*/

  /* write Protracker's ID */
  Whatever[0] = 'M';
  Whatever[1] = '.';
  Whatever[2] = 'K';
  Whatever[3] = '.';
  fwrite ( Whatever , 4 , 1 , out );

  /* read pattern addresses */
  for ( i=0 ; i<64 ; i++ )
  {
    PatAdds[i] = (in_data[Where]*256*256*256)+
                 (in_data[Where+1]*256*256)+
                 (in_data[Where+2]*256)+
                  in_data[Where+3];
    PatAdds[i] += 0x0c;
    Where += 4;
  }

  /* pattern data */
  for ( i=0 ; i<Max ; i++ )
  {
    Where = PW_Start_Address + PatAdds[i];
    for ( k=0 ; k<4 ; k++ )
    {
      TracksAdd[k] = (in_data[Where]*256)+in_data[Where+1];
      Where += 2;
    }

    BZERO ( Whatever , 1024 );
    for ( k=0 ; k<4 ; k++ )
    {
      Where = PW_Start_Address + PatAdds[i]+TracksAdd[k];
      for ( j=0 ; j<64 ; j++ )
      {
        c1 = in_data[Where++];
	if ( (c1&0x80) == 0x80 )
	{
	  j += (c1&0x7F);
	  continue;
	}
        c2 = in_data[Where++];
        c3 = in_data[Where++];

	Smp  = c1&0x1F;
	Note = c2&0x3F;
	Fx   = ((c1>>5)&0x03);
        c4   = ((c2>>4)&0x0C);
        Fx   |= c4;
	FxVal = c3;

	Whatever[j*16+k*4] = (Smp & 0xf0);

        if ( Note != 0 )
        {
          Whatever[j*16+k*4] |= poss[Note-1][0];
          Whatever[j*16+k*4+1] = poss[Note-1][1];
        }

	Whatever[j*16+k*4+2] = ((Smp<<4)&0xf0);
	Whatever[j*16+k*4+2] |= Fx;
	Whatever[j*16+k*4+3] = FxVal;
      }
    }
    fwrite ( Whatever , 1024 , 1 , out );
/*    printf ( "pattern %ld written\n" , i );*/
  }
  free ( Whatever );

  /* sample data */
  for ( i=0 ; i<31 ; i++ )
  {
    Where = PW_Start_Address + SmpDataAdds[i];
    fwrite ( &in_data[Where] , SmpSizes[i] , 1 , out );
  }


  Crap ( " STIM (Slamtilt)  " , BAD , BAD , out );
  /*
  fseek ( out , 830 , SEEK_SET );
  fprintf ( out , " -[Converted with]- " );
  fseek ( out , 860 , SEEK_SET );
  fprintf ( out , " -[STIM packer to]- " );
  fseek ( out , 890 , SEEK_SET );
  fprintf ( out , "   -[ProTracker]-   " );
  fseek ( out , 920 , SEEK_SET );
  fprintf ( out , " -[by Asle /ReDoX]- " );
  */

  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return; /* useless ... but */
}
