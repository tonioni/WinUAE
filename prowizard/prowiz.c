/*
 * Pro-Wizard_1.c
 *
 * 1997-2007 (c) Sylvain "Asle" Chipaux
 *
*/

#include "include/globals.h"
#include "include/extern.h"
#include "include/vars.h"


#if 0
int main ( int ac , char **av )
#else
int prowizard_search (Uchar *in_data_p, int PW_in_size_p)
#endif
{
  Support_Types_FileDefault ();
  in_data = in_data_p;
  PW_in_size = PW_in_size_p;
#if 0
  printf ( "\n\n-<([ Pro-Wizard v1.62 ])>-\n\n" );

  if ( ac != 2 )
  {
    printf ( "%s <data file>\n" , av[0] );
    printf ( "Check for the documentation for more info !\n" );
    exit ( 0 );
  }

  PW_in = fopen ( av[1] , "rb" );
  if ( PW_in == NULL )
  {
    printf ( "cant find \"%s\" !\n" , av[1] );
    exit ( 0 );
  }

  /* take care of the editable extensions */
  Support_Types ();
  /*printf ( "%x,%x,%x\n" , Extensions[70][0], Extensions[70][1], Extensions[70][2] );*/

  /* get input file size */
  PW_in_size = PWGetFileSize (av[1]);
  fseek ( PW_in , 0 , 0 ); /* probably useless */
  printf ( "input file size : %ld\n" , PW_in_size );
  if ( PW_in_size < MINIMAL_FILE_LENGHT )
  {
    printf ( "! input file size is too small ...\n" );
    fclose ( PW_in );
    exit ( 1 );
  }

  /* alloc mem */
  in_data = (Uchar *) malloc ( PW_in_size );
  if ( in_data == NULL )
  {
    perror ( "Couldn't allocate memory" );
    exit ( 0 );
  }
  fread ( in_data , PW_in_size , 1 , PW_in );
  fclose ( PW_in );
#endif

  /********************************************************************/
  /**************************   SEARCH   ******************************/
  /********************************************************************/
  for ( PW_i=0 ; PW_i<(PW_in_size-MINIMAL_FILE_LENGHT) ; PW_i+=1 )
  {
    /* display where we are every 10 Kbytes */
    /*    if ( (PW_i%10240) == 0 )*/
    /*    {*/
      /* ... and rewrites on itself. */
    /*      printf ( "\r%ld", PW_i );*/
      /* force printing on stdout (i.e. the screen) */
    /*      fflush ( stdout );*/
    /*    }*/

    /*******************************************************************/
    /* ok, now the real job starts here. So, we look for ID or Volume  */
    /* values. A simple switch case follows .. based on Hex values of, */
    /* as foretold, ID or volume (not all file have ID ... few in fact */
    /* do ..).                                                         */
    /*******************************************************************/

    if ( in_data[PW_i] <= 0x40 )
    {
      /* try to get rid of those 00 hanging all the time everywhere :(
	 */
      if ( in_data[PW_i] == 0x00 )
      {
	for ( PW_j = 0 ; PW_j<MINIMAL_FILE_LENGHT ; PW_j++)
	{
	  if ( in_data[PW_j+PW_i] != 0x00 )
	    break;
	}
	if ( PW_j == MINIMAL_FILE_LENGHT )
	{
	  PW_i += (MINIMAL_FILE_LENGHT-2);
	  continue;
	}
      }

      /* first, let's take care of the formats with 'ID' value <= 0x40 */
      /* "!PM!" : ID of Power Music */
      if ( (in_data[PW_i]   == '!') &&
           (in_data[PW_i+1] == 'P') &&
           (in_data[PW_i+2] == 'M') &&
           (in_data[PW_i+3] == '!') )
      {
        if ( testPM() != BAD )
        {
          Rip_PM ();
          Depack_PM ();
          continue;
        }
      }
      /* Treasure Patterns ?*/
      /* 0x4000 : ID of Treasure Patterns ? not sure */
      /*
      if ( (in_data[PW_i]   == 0x00) )
      {
        if ( testTreasure() != BAD )
        {
          Rip_Treasure ();
          Depack_Treasure ();
          continue;
        }
      }
      */
#ifdef INCLUDEALL
      /* StoneCracker 2.92 data (ex-$08090A08 data cruncher) */
      if ( (in_data[PW_i]   == 0x08) &&
           (in_data[PW_i+1] == 0x09) &&
           (in_data[PW_i+2] == 0x0A) &&
          ((in_data[PW_i+3] == 0x08) || 
	   (in_data[PW_i+3] == 0x0A)))
      {
        if ( testSpecialCruncherData ( 8, 4 ) != BAD )
        {
          Rip_SpecialCruncherData ( "StoneCracker 2.92 Data Cruncher" , 12 , STC292data );
          continue;
        }
      }
      /* "1AM" data cruncher */
      if ( (in_data[PW_i]   == '1') &&
           (in_data[PW_i+1] == 'A') &&
           (in_data[PW_i+2] == 'M') )
      {
        if ( testSpecialCruncherData( 12, 8 ) != BAD )
        {
          Rip_SpecialCruncherData ( "Amnesty Design (1AM) Data Cruncher" , 16 , AmnestyDesign1 );
          continue;
        }
      }
      /* "2AM" data cruncher */
      if ( (in_data[PW_i]   == '2') &&
           (in_data[PW_i+1] == 'A') &&
           (in_data[PW_i+2] == 'M') )
      {
        if ( testSpecialCruncherData ( 8, 4 ) != BAD )
        {
          Rip_SpecialCruncherData ( "Amnesty Design (2AM) Data Cruncher" , 12 , AmnestyDesign2 );
          continue;
        }
      }
#endif
      /* "[1-9]CHN" FastTracker v1 */
      if ( ((in_data[PW_i]   == '1') ||
	    (in_data[PW_i]   == '2') ||
	    (in_data[PW_i]   == '3') ||
	    (in_data[PW_i]   == '4') ||
	    (in_data[PW_i]   == '5') ||
	    (in_data[PW_i]   == '6') ||
	    (in_data[PW_i]   == '7') ||
	    (in_data[PW_i]   == '8') ||
	    (in_data[PW_i]   == '9'))&&
           (in_data[PW_i+1] == 'C') &&
           (in_data[PW_i+2] == 'H') &&
           (in_data[PW_i+3] == 'N') )
      {
        if ( testMOD(in_data[PW_i]-0x30) != BAD )
        {
          Rip_MOD (in_data[PW_i]-0x30);
          continue;
        }
      }
      /* "[10-32]CH" FastTracker v1/v2 */
      if ( ((((in_data[PW_i]   == '1') || (in_data[PW_i]   == '2')) && 
	     ((in_data[PW_i+1]   == '0') ||
	      (in_data[PW_i+1]   == '1') ||
	      (in_data[PW_i+1]   == '2') ||
	      (in_data[PW_i+1]   == '3') ||
	      (in_data[PW_i+1]   == '4') ||
	      (in_data[PW_i+1]   == '5') ||
	      (in_data[PW_i+1]   == '6') ||
	      (in_data[PW_i+1]   == '7') ||
	      (in_data[PW_i+1]   == '8') ||
	      (in_data[PW_i+1]   == '9'))) ||
	    ((in_data[PW_i]   == '3') &&
	     ((in_data[PW_i+1]   == '0') ||
	      (in_data[PW_i+1]   == '1')))) &&
	    (in_data[PW_i+2] == 'C') &&
	    (in_data[PW_i+3] == 'H') )
      {
        if ( testMOD((in_data[PW_i]-0x30)*10+in_data[PW_i+1]-0x30) != BAD )
        {
          Rip_MOD ((in_data[PW_i]-0x30)*10+in_data[PW_i+1]-0x30);
          continue;
        }
      }
#ifdef INCLUDEALL
      /* =SB= data cruncher */
      if ( (in_data[PW_i]   == 0x3D) &&
           (in_data[PW_i+1] == 'S') &&
           (in_data[PW_i+2] == 'B') &&
           (in_data[PW_i+3] == 0x3D) )
      {
        if ( testSpecialCruncherData ( 8, 4 ) != BAD )
        {
          Rip_SpecialCruncherData ( "=SB= Data Cruncher" , 12 , SB_DataCruncher );
          continue;
        }
      }

      /* -CJ- data cruncher (CrackerJack/Mirage)*/
      if ( (in_data[PW_i]   == 0x2D) &&
           (in_data[PW_i+1] == 'C') &&
           (in_data[PW_i+2] == 'J') &&
           (in_data[PW_i+3] == 0x2D) )
      {
        if ( testSpecialCruncherData ( 4, 8 ) != BAD )
        {
          Rip_SpecialCruncherData ( "-CJ- Data Cruncher" , 0 , CJ_DataCruncher );
          continue;
        }
      }

      /* -GD- Skizzo*/
      if ( (in_data[PW_i]   == 0x2D) &&
           (in_data[PW_i+1] == 'G') &&
           (in_data[PW_i+2] == 'D') &&
           (in_data[PW_i+3] == 0x2D) )
      {
        if ( testSkizzo() != BAD )
        {
          Rip_Skizzo ();
          Depack_Skizzo ();
          continue;
        }
      }

      /* Max Packer 1.2 */
      if ((in_data[PW_i]   == 0x28) &&
         (in_data[PW_i+1]  == 0x3C) &&
         (in_data[PW_i+6]  == 0x26) &&
         (in_data[PW_i+7]  == 0x7A) &&
         (in_data[PW_i+8]  == 0x01) &&
         (in_data[PW_i+9]  == 0x6C) &&
         (in_data[PW_i+10] == 0x41) &&
         (in_data[PW_i+11] == 0xFA) &&
         (in_data[PW_i+12] == 0x01) &&
         (in_data[PW_i+13] == 0x7C) &&
         (in_data[PW_i+14] == 0xD1) &&
         (in_data[PW_i+15] == 0xFA) )
      {
        if ( testMaxPacker12() == BAD )
          break;
        Rip_MaxPacker12 ();
        continue;
      }
#endif

      /* XANN packer */
      if ( in_data[PW_i] == 0x3c )
      {
        if ( testXANN() != BAD )
        {
          Rip_XANN ();
          Depack_XANN ();
          continue;
        }
      }

      /* hum ... that's where things become interresting :) */
      /* Module Protector without ID */
      /* LEAVE IT THERE !!! ... at least before Heatseeker format since they are VERY similare ! */
      if ( testMP_noID() != BAD )
      {
        Rip_MP_noID ();
        Depack_MP ();
        continue;
      }

      /* Digital Illusion */
      if ( testDI() != BAD )
      {
        Rip_DI ();
        Depack_DI ();
        continue;
      }

      /* SGTPacker */
      /*
      if ( testSGT() != BAD )
      {
        Rip_SGT ();
        Depack_SGT ();
        continue;
      }
      */
      /* eureka packer */
      if ( testEUREKA() != BAD )
      {
        Rip_EUREKA ();
        Depack_EUREKA ();
        continue;
      }

      /* The player 5.0a ? */
      if ( testP50A() != BAD )
      {
        Rip_P50A ();
        Depack_P50A ();
        continue;
      }

      /* The player 6.0a ? */
      if ( testP60A_nopack() != BAD )
      {
        Rip_P60A ();
        Depack_P60A ();
        continue;
      }

      /* The player 6.0a (packed samples)? */
      if ( testP60A_pack() != BAD )
      {
        printf ( "\b\b\b\b\b\b\b\bThe Player 6.0A with PACKED samples found at %ld ... cant rip it!\n" , PW_Start_Address );
        /*Rip_P60A ();*/
        /*Depack_P60A ();*/
        continue;
      }

      /* The player 6.1a ? */
      if ( testP61A_nopack() != BAD )
      {
        Rip_P61A ();
        Depack_P61A ();
        continue;
      }

      /* The player 6.1a (packed samples)? */
      if ( testP61A_pack() != BAD )
      {
        printf ( "\b\b\b\b\b\b\b\bThe Player 6.1A with PACKED samples found at %ld ... cant rip it!\n" , PW_Start_Address );
        /*Rip_P61A ();*/
        /*Depack_P61A ();*/
        continue;
      }

      /* Propacker 1.0 */
      if ( testPP10() != BAD )
      {
        Rip_PP10 ();
        Depack_PP10 ();
        continue;
      }

      /* Noise Packer v2 */
      /* LEAVE VERSION 2 BEFORE VERSION 1 !!!!! */
      if ( testNoisepacker2() != BAD )
      {
        Rip_Noisepacker2 ();
        Depack_Noisepacker2 ();
        continue;
      }

      /* Noise Packer v1 */
      if ( testNoisepacker1() != BAD )
      {
        Rip_Noisepacker1 ();
        Depack_Noisepacker1 ();
        continue;
      }

      /* Noise Packer v3 */
      if ( testNoisepacker3() != BAD )
      {
        Rip_Noisepacker3 ();
        Depack_Noisepacker3 ();
        continue;
      }

      /* Promizer 0.1 */
      if ( testPM01() != BAD )
      {
        Rip_PM01 ();
        Depack_PM01 ();
        continue;
      }

      /* ProPacker 2.1 */
      if ( testPP21() != BAD )
      {
        Rip_PP21 ();
        Depack_PP21 ();
        continue;
      }

      /* ProPacker 3.0 */
      if ( testPP30() != BAD )
      {
        Rip_PP30 ();
        Depack_PP30 ();
        continue;
      }

      /* StartTrekker pack */
      if ( testSTARPACK() != BAD )
      {
        Rip_STARPACK ();
        Depack_STARPACK ();
        continue;
      }

      /* Zen packer */
      if ( testZEN() != BAD )
      {
        Rip_ZEN ();
        Depack_ZEN ();
        continue;
      }

      /* Unic tracker v1 ? */
      if ( testUNIC_withemptyID() != BAD )
      {
        Rip_UNIC_withID ();
        Depack_UNIC ();
        continue;
      }

      /* Unic tracker v1 ? */
      if ( testUNIC_noID() != BAD )
      {
        Rip_UNIC_noID ();
        Depack_UNIC ();
        continue;
      }

      /* Unic trecker v2 ? */
      if ( testUNIC2() != BAD )
      {
        Rip_UNIC2 ();
        Depack_UNIC2 ();
        continue;
      }

      /* Game Music Creator ? */
      if ( testGMC() != BAD )
      {
        Rip_GMC ();
        Depack_GMC ();
        continue;
      }

      /* Heatseeker ? */
      if ( testHEATSEEKER() != BAD )
      {
        Rip_HEATSEEKER ();
        Depack_HEATSEEKER ();
        continue;
      }

      /* SoundTracker (15 smp) */
      if ( testSoundTracker() != BAD )
      {
        Rip_SoundTracker ();
        continue;
      }

      /* The Dark Demon (group name) format */
      if ( testTheDarkDemon() != BAD )
      {
        Rip_TheDarkDemon ();
        Depack_TheDarkDemon ();
        continue;
      }

      /* Newtron */
      if ( testNewtron() != BAD )
      {
        Rip_Newtron ();
        Depack_Newtron ();
        continue;
      }

      /* Newtron Old */
      if ( testNewtronOld() != BAD )
      {
        Rip_NewtronOld ();
        Depack_NewtronOld ();
        continue;
      }

      /* Titanics Packer ? */
      if ( testTitanicsPlayer() != BAD )
      {
        Rip_TitanicsPlayer ();
        Depack_TitanicsPlayer ();
        continue;
      }
    }


    /**********************************/
    /* ok, now, the files with ID ... */
    /**********************************/
    switch ( in_data[PW_i] )
    {
      case 'A': /* ATN! another Imploder case */
#ifdef INCLUDEALL
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          break;
        }
#endif
	/* AMOS Music bank "AmBk" */
        if ( (in_data[PW_i+1] == 'm') &&
             (in_data[PW_i+2] == 'B') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testAmBk() == BAD )
            break;
          Rip_AmBk();
          Depack_AmBk();
          break;
        }
#ifdef INCLUDEALL
        /* Time Cruncher 1.7 */
        if ( (in_data[PW_i+1]  == 0xFA) &&
             (in_data[PW_i+2]  == 0x01) &&
             (in_data[PW_i+3]  == 0x34) &&
             (in_data[PW_i+4]  == 0xD1) &&
             (in_data[PW_i+5]  == 0xFC) &&
             (in_data[PW_i+10] == 0x43) &&
             (in_data[PW_i+11] == 0xF9) &&
             (in_data[PW_i+16] == 0x24) &&
             (in_data[PW_i+17] == 0x60) &&
             (in_data[PW_i+18] == 0xD5) &&
             (in_data[PW_i+19] == 0xC9) &&
             (in_data[PW_i+20] == 0x20) &&
             (in_data[PW_i+21] == 0x20) )
        {
          if ( testTimeCruncher17() == BAD )
            break;
          Rip_TimeCruncher17 ();
          break;
        }
        /* IAM Cruncher 1.0 (another case (aka ICE)) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'M') &&
             (in_data[PW_i+3] == '5') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "IAM Packer 1.0 (ATM5) data" , 12 , ICE );
          break;
        }
        /* ATOM - Atomik Packer (Atari ST) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Atomik Packer (ATOM) data" , 12 , AtomikPackerData );
          break;
        }
        /* ATM3 - Atomik Packer (Atari ST) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'M') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Atomik Packer (ATOM) data" , 12 , AtomikPackerData );
          break;
        }
          /* "AU5!" - Automation Packer 5.* (Atari ST) */
        if ( (in_data[PW_i+1] == 'U') &&
             (in_data[PW_i+2] == '5') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Automation Packer v5.01 (data)" , 0 , AutomationPackerData );
          break;
        }
        /* Syncro Packer 4.6 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x01 ) &&
             (in_data[PW_i+3]  == 0x66 ) &&
             (in_data[PW_i+4]  == 0x22 ) &&
             (in_data[PW_i+5]  == 0x58 ) &&
             (in_data[PW_i+6]  == 0x20 ) &&
             (in_data[PW_i+7]  == 0x18 ) &&
             (in_data[PW_i+8]  == 0x26 ) &&
             (in_data[PW_i+9]  == 0x48 ) &&
             (in_data[PW_i+10] == 0xD1 ) &&
             (in_data[PW_i+11] == 0xC0 ) &&
             (in_data[PW_i+12] == 0x1E ) &&
             (in_data[PW_i+13] == 0x20 ) &&
             (in_data[PW_i+14] == 0x1C ) &&
             (in_data[PW_i+15] == 0x20 ) )
        {
          if ( testSyncroPacker() != BAD )
          {
            Rip_SyncroPacker ();
            break;
          }
          break;
        }
        /* Tetrapack 1.02 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xE6 ) &&
             (in_data[PW_i+4]  == 0xD1 ) &&
             (in_data[PW_i+5]  == 0xFC ) &&
             (in_data[PW_i+10] == 0x22 ) &&
             (in_data[PW_i+11] == 0x7C ) &&
             (in_data[PW_i+16] == 0x24 ) &&
             (in_data[PW_i+17] == 0x60 ) &&
             (in_data[PW_i+18] == 0xD5 ) &&
             (in_data[PW_i+19] == 0xC9 ) )
        {
          if ( testTetrapack102() == BAD )
            break;
          Rip_Tetrapack102 ();
        }
        /* "ArcD" data cruncher */
        if ( (in_data[PW_i+1]  == 'r') &&
             (in_data[PW_i+2]  == 'c') &&
             (in_data[PW_i+3]  == 'D'))
        {
          if ( testArcDDataCruncher() == BAD )
            break;
          Rip_SpecialCruncherData ( "ArcD data Cruncher" , 0 , arcD );
          break;
        }
        /* HQC Cruncher 2.0 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x06 ) &&
             (in_data[PW_i+3]  == 0x76 ) &&
             (in_data[PW_i+4]  == 0x20 ) &&
             (in_data[PW_i+5]  == 0x80 ) &&
             (in_data[PW_i+6]  == 0x41 ) &&
             (in_data[PW_i+7]  == 0xFA ) &&
             (in_data[PW_i+8]  == 0x06 ) &&
             (in_data[PW_i+9]  == 0x64 ) &&
             (in_data[PW_i+10] == 0x43 ) &&
             (in_data[PW_i+11] == 0xFA ) &&
             (in_data[PW_i+12] == 0x05 ) &&
             (in_data[PW_i+13] == 0x10 ) &&
             (in_data[PW_i+14] == 0x20 ) &&
             (in_data[PW_i+15] == 0x89 ) )
        {
          if ( testHQCCruncher2() != BAD )
          {
            Rip_HQCCruncher2 ();
            break;
          }
          break;
        }
        /* ByteKillerPro 1.0 */
        if ( (in_data[PW_i+1]  == 0xFA) &&
             (in_data[PW_i+2]  == 0x00) &&
             (in_data[PW_i+3]  == 0xDC) &&
             (in_data[PW_i+4]  == 0x2C) &&
             (in_data[PW_i+5]  == 0x78) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x04) &&
             (in_data[PW_i+12] == 0x43) &&
             (in_data[PW_i+13] == 0xF9) &&
             (in_data[PW_i+18] == 0x20) &&
             (in_data[PW_i+19] == 0x10) &&
             (in_data[PW_i+20] == 0x22) &&
             (in_data[PW_i+21] == 0x28) )
        {
          if ( testbytekillerpro10() == BAD )
            break;
          Rip_bytekillerpro10 ();
          break;
        }
	/* Ace? (data cruncher) */
        if ( (in_data[PW_i+1] == 'c') &&
             (in_data[PW_i+2] == 'e') &&
             (in_data[PW_i+3] == '?') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "(Ace?) Data Cruncher" , 0 , ACECruncherData );
          break;
        }
#endif
        break;

      case 'B': /* BTB6 */
#ifdef INCLUDEALL
        /* ByteKiller 1.3 (exepack) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'B') &&
             (in_data[PW_i+3] == '6') )
        {
          if ( testByteKiller_13() != BAD )
          {
            Rip_ByteKiller ();
            break;
          }
          testByteKiller_20 ();
          if ( testByteKiller_20() != BAD )
          {
            Rip_ByteKiller ();
            break;
          }
          break;
        }
#endif
        /* "BeEp" Jam Cracker */
        if ( (in_data[PW_i+1] == 'e') &&
             (in_data[PW_i+2] == 'E') &&
             (in_data[PW_i+3] == 'p') )
        {
          if ( testJamCracker() == BAD )
            break;
          Rip_JamCracker ();
          break;
        }
        break;

      case 'C': /* 0x43 */
      /* CPLX_TP3 ?!? */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'L') &&
             (in_data[PW_i+3] == 'X') &&
             (in_data[PW_i+4] == '_') &&
             (in_data[PW_i+5] == 'T') &&
             (in_data[PW_i+6] == 'P') &&
             (in_data[PW_i+7] == '3') )
        {
          if ( testTP3() == BAD )
            break;
          Rip_TP3 ();
          Depack_TP3 ();
          break;
        }
#ifdef INCLUDEALL
        /* CrM2 | Crm2 | CrM! */
        if ( ((in_data[PW_i+1] == 'r') &&
              (in_data[PW_i+2] == 'M') &&
              (in_data[PW_i+3] == '2'))||
             ((in_data[PW_i+1] == 'r') &&
              (in_data[PW_i+2] == 'm') &&
              (in_data[PW_i+3] == '2'))||
             ((in_data[PW_i+1] == 'r') &&
              (in_data[PW_i+2] == 'M') &&
              (in_data[PW_i+3] == '!')) )
        {
          if ( testSpecialCruncherData ( 10, 6 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Crunchmania / Normal data" , 14 , CRM1 );
          break;
        }
        /* "CHFI"  another imploder case */
        if ( (in_data[PW_i+1] == 'H') &&
             (in_data[PW_i+2] == 'F') &&
             (in_data[PW_i+3] == 'I') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          break;
        }
        /* "CRND" data cruncher */
        if ( (in_data[PW_i+1] == 'R') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testCRND() == BAD )
            break;
          Rip_SpecialCruncherData ( "CRND data cruncher" , 20 , CRND );
          break;
        }
	/* Defjam Cruncher 3.2 */
        if ( (in_data[PW_i+1] == 0xFA) &&
             (in_data[PW_i+2] == 0x02) &&
             (in_data[PW_i+3] == 0x8C) &&
             ((in_data[PW_i+4] == 0x4B) || (in_data[PW_i+4] == 0x9B))&&
             ((in_data[PW_i+5] == 0xF9) || (in_data[PW_i+5] == 0xCD))&&
             ((in_data[PW_i+6] == 0x00) || (in_data[PW_i+6] == 0x4E)))
        {
          if ( testDefjam32() == BAD )
            break;
          Rip_Defjam32 ();
          break;
        }
#endif
        break;

      case 'D': /* 0x44 */
        /* Digibooster 1.7 */
        if ( (in_data[PW_i+1] == 'I') &&
	     (in_data[PW_i+2] == 'G') &&
	     (in_data[PW_i+3] == 'I') )
        {
          if ( testDigiBooster17() == BAD )
            break;
          Rip_DigiBooster17 ();
          break;
        }
        break;

      case 'E': /* 0x45 */
          /* "EMOD" : ID of Quadra Composer */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testQuadraComposer() == BAD )
            break;
          Rip_QuadraComposer ();
          Depack_QuadraComposer ();
          break;
        }
          /* "Extended Module" : ID of FastTracker 2 XM */
        if ( (in_data[PW_i+1] == 'x') &&
             (in_data[PW_i+2] == 't') &&
             (in_data[PW_i+3] == 'e') &&
             (in_data[PW_i+4] == 'n') &&
             (in_data[PW_i+5] == 'd') &&
             (in_data[PW_i+6] == 'e') &&
             (in_data[PW_i+7] == 'd') &&
             (in_data[PW_i+8] == ' ') &&
             (in_data[PW_i+9] == 'M') &&
             (in_data[PW_i+10]== 'o') )
        {
          if ( testXM() == BAD )
            break;
          Rip_XM ();
          break;
        }
        break;

      case 'F': /* 0x46 */
          /* "FC-M" : ID of FC-M packer */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == '-') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testFC_M() == BAD )
            break;
          Rip_FC_M ();
          Depack_FC_M ();
          break;
        }
          /* "FLT4" : ID of StarTrekker */
        if ( (in_data[PW_i+1] == 'L') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testMOD(4) == BAD )
            break;
          Rip_MOD (4);
          break;
        }
          /* "FC14" : Future Composer 1.4 */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testFC14() == BAD )
            break;
          Rip_FC14 ();
          break;
        }
          /* "FUCO" : ID of BSI Future Composer */
        if ( (in_data[PW_i+1] == 'U') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == 'O') )
        {
          if ( testBSIFutureComposer() == BAD )
            break;
          Rip_BSIFutureComposer ();
          break;
        }
         /* "Fuck" : ID of Noise From Heaven chiptunes */
        if ( (in_data[PW_i+1] == 'u') &&
             (in_data[PW_i+2] == 'c') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testNFH() == BAD )
            break;
          Rip_NFH ();
	      Depack_NFH ();
          break;
        }
    	  /* "FAST" : ID of Stone Arts Player */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'S') &&
             (in_data[PW_i+3] == 'T') )
        {
          if ( testStoneArtsPlayer() == BAD )
            break;
          Rip_StoneArtsPlayer ();
	      Depack_StoneArtsPlayer ();
          break;
        }
#ifdef INCLUDEALL
          /* FIRE (RNC clone) Cruncher */
        if ( (in_data[PW_i+1] == 'I') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'E') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "FIRE (RNC Clone) data Cruncher" , 0 , FIRE );
          break;
        }
#endif
        break;

      case 'G': /* 0x47 */
#ifdef INCLUDEALL
        /* Mega Cruncher 1.0 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x01 ) &&
             (in_data[PW_i+3]  == 0x2E ) &&
             (in_data[PW_i+4]  == 0x20 ) &&
             (in_data[PW_i+5]  == 0x0B ) &&
             (in_data[PW_i+6]  == 0x22 ) &&
             (in_data[PW_i+7]  == 0x2B ) &&
             (in_data[PW_i+8]  == 0x00 ) &&
             (in_data[PW_i+9]  == 0x08 ) &&
             (in_data[PW_i+10] == 0x41 ) &&
             (in_data[PW_i+11] == 0xFA ) &&
             (in_data[PW_i+12] == 0x01 ) &&
             (in_data[PW_i+13] == 0x30 ) &&
             (in_data[PW_i+14] == 0xD1 ) &&
             (in_data[PW_i+15] == 0xC1 ) )
        {
          if ( testMegaCruncher10() != BAD )
          {
            Rip_MegaCruncher ();
            break;
          }
          break;
        }

        /* Mega Cruncher 1.2 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x01 ) &&
             (in_data[PW_i+3]  == 0x32 ) &&
             (in_data[PW_i+4]  == 0x20 ) &&
             (in_data[PW_i+5]  == 0x0B ) &&
             (in_data[PW_i+6]  == 0x22 ) &&
             (in_data[PW_i+7]  == 0x2B ) &&
             (in_data[PW_i+8]  == 0x00 ) &&
             (in_data[PW_i+9]  == 0x08 ) &&
             (in_data[PW_i+10] == 0x41 ) &&
             (in_data[PW_i+11] == 0xFA ) &&
             (in_data[PW_i+12] == 0x01 ) &&
             (in_data[PW_i+13] == 0x34 ) &&
             (in_data[PW_i+14] == 0xD1 ) &&
             (in_data[PW_i+15] == 0xC1 ) )
        {
          if ( testMegaCruncher12() == BAD )
	    break;
	  Rip_MegaCruncher ();
	  break;
        }

        /* Double Action v1.0 */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+137]== 0xAB ) &&
             (in_data[PW_i+138]== 0xD1 ) &&
             (in_data[PW_i+139]== 0xC0 ) &&
             (in_data[PW_i+140]== 0xD3 ) &&
             (in_data[PW_i+141]== 0xC0 ) &&
             (in_data[PW_i+142]== 0x23 ) &&
             (in_data[PW_i+143]== 0x20 ) )
        {
          if ( testDoubleAction10() == BAD )
            break;
          Rip_DoubleAction10 ();
          break;
        }
#endif
	/* GPMO (crunch player ?)*/
        if ( (in_data[PW_i+1] == 'P') &&
           (in_data[PW_i+2] == 'M') &&
           (in_data[PW_i+3] == 'O') )
        {
          if ( testGPMO() == BAD )
            break;
          Rip_GPMO ();
          Depack_GPMO ();
          break;
        }

	/* Gnu player */
        if ( (in_data[PW_i+1] == 'n') &&
           (in_data[PW_i+2] == 'P') &&
           (in_data[PW_i+3] == 'l') )
        {
          if ( testGnuPlayer() == BAD )
            break;
          Rip_GnuPlayer ();
          Depack_GnuPlayer ();
          break;
        }

        break;

      case 'H': /* 0x48 */
          /* "HRT!" : ID of Hornet packer */
        if ( (in_data[PW_i+1] == 'R') &&
           (in_data[PW_i+2] == 'T') &&
           (in_data[PW_i+3] == '!') )
        {
          if ( testHRT() == BAD )
            break;
          Rip_HRT ();
          Depack_HRT ();
          break;
        }

#ifdef INCLUDEALL
        /* Master Cruncher 3.0 Address */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x4B) &&
             (in_data[PW_i+5]  == 0xFA) &&
             (in_data[PW_i+6]  == 0x01) &&
             (in_data[PW_i+7]  == 0x80) &&
             (in_data[PW_i+8]  == 0x41) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0xFF) &&
             (in_data[PW_i+11] == 0xF2) &&
             (in_data[PW_i+12] == 0x22) &&
             (in_data[PW_i+13] == 0x50) &&
             (in_data[PW_i+14] == 0xD3) &&
             (in_data[PW_i+15] == 0xC9) )
        {
          if ( testMasterCruncher30addr() == BAD )
            break;
          Rip_MasterCruncher30addr ();
          break;
        }

        /* Powerpacker 4.0 library */
        if ( (in_data[PW_i+1]  == 0x7A) &&
             (in_data[PW_i+2]  == 0x00) &&
             (in_data[PW_i+3]  == 0x58) &&
             (in_data[PW_i+4]  == 0x48) &&
             (in_data[PW_i+5]  == 0xE7) &&
             (in_data[PW_i+6]  == 0xFF) &&
             (in_data[PW_i+7]  == 0xFE) &&
             (in_data[PW_i+8]  == 0x70) &&
             (in_data[PW_i+9]  == 0x23) &&
             (in_data[PW_i+10] == 0x43) &&
             (in_data[PW_i+11] == 0xFA) &&
             (in_data[PW_i+12] == 0x00) &&
             (in_data[PW_i+13] == 0x50) &&
             (in_data[PW_i+14] == 0x2C) &&
             (in_data[PW_i+15] == 0x78) )
        {
          if ( testPowerpacker4lib() == BAD )
            break;
          Rip_Powerpacker4lib ();
          break;
        }
        /* StoneCracker 2.70 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x4D) &&
             (in_data[PW_i+5]  == 0xF9) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0xDF) &&
             (in_data[PW_i+8]  == 0xF0) &&
             (in_data[PW_i+9]  == 0x06) &&
             (in_data[PW_i+10] == 0x7E) &&
             (in_data[PW_i+11] == 0x00) &&
             (in_data[PW_i+12] == 0x7C) &&
             (in_data[PW_i+13] == 0x00) &&
             (in_data[PW_i+14] == 0x7A) &&
             (in_data[PW_i+15] == 0x00) )
        {
          if ( testStoneCracker270() == BAD )
            break;
          Rip_StoneCracker270 ();
          break;
        }

        /* ByteKiller 3.0 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x41) &&
             (in_data[PW_i+5]  == 0xFA) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0xB6) &&
             (in_data[PW_i+8]  == 0x43) &&
             (in_data[PW_i+9]  == 0xF9) &&
             (in_data[PW_i+14] == 0x4D) &&
             (in_data[PW_i+15] == 0xF9) )
        {
          if ( testByteKiller30() == BAD )
            break;
          Rip_ByteKiller30 ();
          break;
        }

        /* Powerpacker 2.3 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x41) &&
             (in_data[PW_i+5]  == 0xFA) &&
             (in_data[PW_i+6]  == 0xFF) &&
             (in_data[PW_i+7]  == 0xF6) &&
             (in_data[PW_i+8]  == 0x20) &&
             (in_data[PW_i+9]  == 0x50) &&
             (in_data[PW_i+10] == 0xD1) &&
             (in_data[PW_i+11] == 0xC8) &&
             (in_data[PW_i+12] == 0xD1) &&
             (in_data[PW_i+13] == 0xC8) &&
             (in_data[PW_i+14] == 0x4A) &&
             (in_data[PW_i+15] == 0x98) )
        {
          if ( testPowerpacker23() == BAD )
            break;
          Rip_Powerpacker23 ();
          break;
        }

        /* Powerpacker 3.0 */
        if ( (in_data[PW_i+1]  == 0x7A) &&
             (in_data[PW_i+2]  == 0x01) &&
             (in_data[PW_i+3]  == 0x78) &&
             (in_data[PW_i+4]  == 0x48) &&
             (in_data[PW_i+5]  == 0xE7) &&
             (in_data[PW_i+6]  == 0xFF) &&
             (in_data[PW_i+7]  == 0xFE) &&
             (in_data[PW_i+8]  == 0x49) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0xFF) &&
             (in_data[PW_i+11] == 0xF2) &&
             (in_data[PW_i+12] == 0x20) &&
             (in_data[PW_i+13] == 0x54) &&
             (in_data[PW_i+14] == 0xD1) &&
             (in_data[PW_i+15] == 0xC8) )
        {
          if ( testPowerpacker30() == BAD )
            break;
          Rip_Powerpacker30 ();
          break;
        }

        /* Powerpacker 4.0 */
        if ( (in_data[PW_i+1]  == 0x7A) &&
             (in_data[PW_i+2]  == 0x01) &&
             (in_data[PW_i+3]  == 0xC8) &&
             (in_data[PW_i+4]  == 0x48) &&
             (in_data[PW_i+5]  == 0xE7) &&
             (in_data[PW_i+6]  == 0xFF) &&
             (in_data[PW_i+7]  == 0xFE) &&
             (in_data[PW_i+8]  == 0x49) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0xFF) &&
             (in_data[PW_i+11] == 0xF2) &&
             (in_data[PW_i+12] == 0x20) &&
             (in_data[PW_i+13] == 0x54) &&
             (in_data[PW_i+14] == 0xD1) &&
             (in_data[PW_i+15] == 0xC8) )
        {
          if ( testPowerpacker40() == BAD )
            break;
          Rip_Powerpacker40 ();
          break;
        }

        /* Super Cruncher 2.7 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x2C) &&
             (in_data[PW_i+5]  == 0x79) &&
             (in_data[PW_i+10] == 0x4E) &&
             (in_data[PW_i+11] == 0xAE) &&
             (in_data[PW_i+12] == 0xFF) &&
             (in_data[PW_i+13] == 0x7C) &&
             (in_data[PW_i+14] == 0x41) &&
             (in_data[PW_i+15] == 0xFA) )
        {
          if ( testSuperCruncher27() == BAD )
            break;
          Rip_SuperCruncher27 ();
          break;
        }

        /* Crunchmania Address */
        if ((in_data[PW_i+1] == 0xe7) &&
           (in_data[PW_i+14] == 0x22) &&
           (in_data[PW_i+15] == 0x1A) &&
           (in_data[PW_i+16] == 0x24) &&
           (in_data[PW_i+17] == 0x1A) &&
           (in_data[PW_i+18] == 0x47) &&
           (in_data[PW_i+19] == 0xEA) &&
           (in_data[PW_i+24] == 0x6F) &&
           (in_data[PW_i+25] == 0x1C) &&
           (in_data[PW_i+26] == 0x26) &&
           (in_data[PW_i+27] == 0x49) &&
           (in_data[PW_i+28] == 0xD7) &&
           (in_data[PW_i+29] == 0xC1) &&
           (in_data[PW_i+30] == 0xB7) &&
           (in_data[PW_i+31] == 0xCA) )
        {
          if ( testcrunchmaniaAddr() == BAD )
            break;
          Rip_CrunchmaniaAddr ();
          continue;
        }

        /* Crunchmania Address (another)*/
        if ((in_data[PW_i+1] == 0xe7) &&
           (in_data[PW_i+14] == 0x20) &&
           (in_data[PW_i+15] == 0x4C) &&
           (in_data[PW_i+16] == 0x47) &&
           (in_data[PW_i+17] == 0xFA) &&
           (in_data[PW_i+18] == 0x00) &&
           (in_data[PW_i+19] == 0x0C) &&
           (in_data[PW_i+24] == 0x51) &&
           (in_data[PW_i+25] == 0xCF) &&
           (in_data[PW_i+26] == 0xFF) &&
           (in_data[PW_i+27] == 0xFC) &&
           (in_data[PW_i+28] == 0x4E) &&
           (in_data[PW_i+29] == 0xD0) &&
           (in_data[PW_i+30] == 0x43) &&
           (in_data[PW_i+31] == 0xF9) )
        {
          if ( testcrunchmaniaAddr() == BAD )
            break;
          Rip_CrunchmaniaAddr ();
          continue;
        }

        /* Crunchmania Simple */
        if ((in_data[PW_i+1] == 0xE7) &&
	    (in_data[PW_i+2] == 0xFF) &&
	    (in_data[PW_i+3] == 0xFF) &&
	    (in_data[PW_i+4] == 0x45) &&
	    (in_data[PW_i+5] == 0xFA) &&
	    (in_data[PW_i+6] == 0x01) &&
	    (in_data[PW_i+7] == 0x66) &&
	    (in_data[PW_i+8] == 0x22) &&
	    (in_data[PW_i+9] == 0x1A) &&
	    (in_data[PW_i+10] == 0x24) &&
	    (in_data[PW_i+11] == 0x1A) &&
	    (in_data[PW_i+12] == 0x22) &&
	    (in_data[PW_i+13] == 0x4A) &&
	    (in_data[PW_i+14] == 0x28) &&
	    (in_data[PW_i+15] == 0x7A) )
	{
          if ( testcrunchmaniaSimple() == BAD )
            break;
          Rip_CrunchmaniaSimple();
          continue;
        }

        /* RelokIt 1.0 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x41) &&
             (in_data[PW_i+5]  == 0xFA) &&
             (in_data[PW_i+6]  == 0x02) &&
             (in_data[PW_i+7]  == 0xC6) &&
             (in_data[PW_i+8]  == 0x70) &&
             (in_data[PW_i+9]  == 0x00) &&
             (in_data[PW_i+10] == 0x30) &&
             (in_data[PW_i+11] == 0x28) &&
             (in_data[PW_i+12] == 0x00) &&
             (in_data[PW_i+13] == 0x04) &&
             (in_data[PW_i+14] == 0x23) &&
             (in_data[PW_i+15] == 0xC0) )
        {
          if ( testRelokIt10() == BAD )
            break;
          Rip_RelokIt10 ();
          break;
        }

        /* Mega Cruncher Obj */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x2C) &&
             (in_data[PW_i+5]  == 0x78) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x04) &&
             (in_data[PW_i+8]  == 0x4B) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0x01) &&
             (in_data[PW_i+11] == 0xC0) )
        {
          if ( testMegaCruncherObj() == BAD )
            break;
          Rip_MegaCruncherObj ();
          break;
        }

        /* Turbo Squeezer 6.1 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x2C) &&
             (in_data[PW_i+5]  == 0x79) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x00) &&
             (in_data[PW_i+8]  == 0x00) &&
             (in_data[PW_i+9]  == 0x04) &&
             (in_data[PW_i+10] == 0x20) &&
             (in_data[PW_i+11] == 0x7A) )
        {
          if ( testTurboSqueezer61() == BAD )
            break;
          Rip_TurboSqueezer61 ();
          break;
        }

        /* DragPack 2.52 */
        if ( (in_data[PW_i+1]  == 0x7A) &&
             (in_data[PW_i+2]  == 0x00) &&
             (in_data[PW_i+3]  == 0x46) &&
             (in_data[PW_i+4]  == 0x48) &&
             (in_data[PW_i+5]  == 0xE7) &&
             (in_data[PW_i+6]  == 0xFF) &&
             (in_data[PW_i+7]  == 0xFE) &&
             (in_data[PW_i+8]  == 0x49) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0xFF) &&
             (in_data[PW_i+11] == 0xEE) &&
             (in_data[PW_i+12] == 0x28) &&
             (in_data[PW_i+13] == 0xFC) &&
             (in_data[PW_i+14] == 0x00) &&
             (in_data[PW_i+15] == 0x00) )
        {
          if ( testDragpack252() == BAD )
            break;
          Rip_Dragpack252 ();
          break;
        }
        /* DragPack 1.00 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x41) &&
             (in_data[PW_i+5]  == 0xF9) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x00) &&
             (in_data[PW_i+8]  == 0x00) &&
             (in_data[PW_i+9]  == 0x00) &&
             (in_data[PW_i+10] == 0x43) &&
             (in_data[PW_i+11] == 0xF9) &&
             (in_data[PW_i+12] == 0x00) &&
             (in_data[PW_i+13] == 0x00) &&
             (in_data[PW_i+14] == 0x00) &&
             (in_data[PW_i+15] == 0x00) )
        {
          if ( testDragpack100() == BAD )
            break;
          Rip_Dragpack100 ();
          break;
        }
        /* GNU Packer 1.2 */
        if ( (in_data[PW_i+1]  == 0xE7) &&
             (in_data[PW_i+2]  == 0xFF) &&
             (in_data[PW_i+3]  == 0xFE) &&
             (in_data[PW_i+4]  == 0x4B) &&
             (in_data[PW_i+5]  == 0xFA) &&
             (in_data[PW_i+6]  == 0x02) &&
             (in_data[PW_i+7]  == 0x32) &&
             (in_data[PW_i+8]  == 0x4D) &&
             (in_data[PW_i+9]  == 0xFA) &&
             (in_data[PW_i+10] == 0x02) &&
             (in_data[PW_i+11] == 0x46) &&
             (in_data[PW_i+12] == 0x20) &&
             (in_data[PW_i+13] == 0x6D) &&
             (in_data[PW_i+14] == 0x00) &&
             (in_data[PW_i+15] == 0x0C) )
        {
          if ( testGNUPacker12() == BAD )
            break;
          Rip_GNUPacker12 ();
          break;
        }
#endif
        break;

      case 'I': /* 0x48 */
#ifdef INCLUDEALL
          /* "ICE!" : ID of IAM packer 1.0 */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == 'E') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "IAM Packer 1.0 (ICE!) data" , 0 , ICE );
          break;
        }
          /* "Ice!" : ID of Ice! Cruncher */
        if ( (in_data[PW_i+1] == 'c') &&
             (in_data[PW_i+2] == 'e') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Ice! Cruncher (data)" , 0 , ICE );
          break;
        }
          /* "IMP!" */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'P') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          break;
        }
#endif
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '0') )
        {
          /* Ice Tracker 1.0 */
          if ( testSTK26() == BAD )
            break;
          Rip_STK26 ();
          Depack_STK26 ();
          break;
        }
        break;

      case 'K': /* 0x4B */
          /* "KRIS" : ID of Chip Tracker */
        if ( (in_data[PW_i+1] == 'R') &&
             (in_data[PW_i+2] == 'I') &&
             (in_data[PW_i+3] == 'S') )
        {
          if ( testKRIS() == BAD )
            break;
          Rip_KRIS ();
          Depack_KRIS ();
          break;
        }
#ifdef INCLUDEALL
        /* Try-It Cruncher 1.01 */
        if ( (in_data[PW_i+1]  == 0xFA) &&
             (in_data[PW_i+2]  == 0x01) &&
             (in_data[PW_i+3]  == 0x54) &&
             (in_data[PW_i+4]  == 0x24) &&
             (in_data[PW_i+5]  == 0x6D) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x18) &&
             (in_data[PW_i+8]  == 0xB3) &&
             (in_data[PW_i+9]  == 0xED) &&
             (in_data[PW_i+10] == 0x00) &&
             (in_data[PW_i+11] == 0x18) &&
             (in_data[PW_i+12] == 0x6F) &&
             (in_data[PW_i+13] == 0x0E) &&
             (in_data[PW_i+14] == 0x20) &&
             (in_data[PW_i+15] == 0x4A) )
        {
          if ( testTryIt101() == BAD )
            break;
          Rip_TryIt101 ();
          break;
        }
#endif
        break;

      case 'M': /* 0x4D */
        if ( (in_data[PW_i+1] == '.') &&
             (in_data[PW_i+2] == 'K') &&
             (in_data[PW_i+3] == '.') )
        {
          /* protracker ? */
          if ( testMOD(4) != BAD )
          {
            Rip_MOD(4);
            break;
          }

          /* Unic tracker v1 ? */
          if ( testUNIC_withID() != BAD )
          {
            Rip_UNIC_withID ();
            Depack_UNIC ();
            break;
          }

          /* Noiserunner ? */
          if ( testNoiserunner() != BAD )
          {
            Rip_Noiserunner ();
            Depack_Noiserunner ();
            break;
          }
        }

        if ( (in_data[PW_i+1] == '1') &&
             (in_data[PW_i+2] == '.') &&
             (in_data[PW_i+3] == '0') )
        {
          /* Fuzzac packer */
          if ( testFUZZAC() != BAD )
          {
            Rip_Fuzzac ();
            Depack_Fuzzac ();
            break;
          }
        }

        if ( (in_data[PW_i+1] == 'E') &&
             (in_data[PW_i+2] == 'X') &&
             (in_data[PW_i+3] == 'X') )
        {
          /* tracker packer v2 */
          if ( testTP2() != BAD )
          {
            Rip_TP2 ();
            Depack_TP2 ();
            break;
          }
          /* tracker packer v1 */
          if ( testTP1() != BAD )
          {
            Rip_TP1 ();
            Depack_TP1 ();
            break;
          }
        }

        if ( in_data[PW_i+1] == '.' )
        {
          /* Kefrens sound machine ? */
          if ( testKSM() != BAD )
          {
            Rip_KSM ();
            Depack_KSM ();
            break;
          }
        }

        /*if ( (in_data[PW_i+1] == 'O') &&
             (in_data[PW_i+2] == 'D') &&
             (in_data[PW_i+3] == 'U') )
        {*/
          /* NovoTrade */
          /*if ( testNovoTrade() != BAD )
          {
            Rip_NovoTrade ();
            Depack_NovoTrade ();
            break;
          }
        }*/

        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == 0x00) )
        {
          /* SoundTracker 2.6 */
          if ( testSTK26() != BAD )
          {
            Rip_STK26 ();
            Depack_STK26 ();
            break;
          }
        }

        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'D') &&
             ((in_data[PW_i+3] == '0') ||
             (in_data[PW_i+3] == '1') || 
             (in_data[PW_i+3] == '2') ||
             (in_data[PW_i+3] == '3')) )
        {
          /* MED (MMD0) */
          if ( testMMD0() == BAD )
            break;
          Rip_MMD0 ();
        }

#ifdef INCLUDEALL
         /* Defjam Cruncher 3.2 pro */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF0 ) &&
             (in_data[PW_i+5]  == 0x00 ) &&
             (in_data[PW_i+6]  == 0x7E ) &&
             (in_data[PW_i+7]  == 0x00 ) &&
             (in_data[PW_i+8]  == 0x30 ) &&
             (in_data[PW_i+9]  == 0x3C ) &&
             (in_data[PW_i+10] == 0x7F ) &&
             (in_data[PW_i+11] == 0xFF ) &&
             (in_data[PW_i+12] == 0x3D ) &&
             (in_data[PW_i+13] == 0x40 ) &&
             (in_data[PW_i+14] == 0x00 ) &&
             (in_data[PW_i+15] == 0x96 ) )
        {
          if ( testDefjam32pro() != BAD )
          {
            Rip_Defjam32 ();
            break;
          }
        }

         /* StoneCracker 2.99d */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF0 ) &&
             (in_data[PW_i+5]  == 0x00 ) &&
             (in_data[PW_i+6]  == 0x4B ) &&
             (in_data[PW_i+7]  == 0xfa ) &&
             (in_data[PW_i+8]  == 0x01 ) &&
             (in_data[PW_i+9]  == 0x54 ) &&
             (in_data[PW_i+10] == 0x49 ) &&
             (in_data[PW_i+11] == 0xf9 ) &&
             (in_data[PW_i+12] == 0x00 ) &&
             (in_data[PW_i+13] == 0xbf ) &&
             (in_data[PW_i+14] == 0xd1 ) &&
             (in_data[PW_i+15] == 0x00 ) )
        {
          if ( testSTC299d() != BAD )
          {
            Rip_STC299d ();
            break;
          }
        }

         /* StoneCracker 2.99b */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF0 ) &&
             (in_data[PW_i+5]  == 0x00 ) &&
             (in_data[PW_i+6]  == 0x4B ) &&
             (in_data[PW_i+7]  == 0xfa ) &&
             (in_data[PW_i+8]  == 0x01 ) &&
             (in_data[PW_i+9]  == 0x58 ) &&
             (in_data[PW_i+10] == 0x49 ) &&
             (in_data[PW_i+11] == 0xf9 ) &&
             (in_data[PW_i+12] == 0x00 ) &&
             (in_data[PW_i+13] == 0xbf ) &&
             (in_data[PW_i+14] == 0xd1 ) &&
             (in_data[PW_i+15] == 0x00 ) )
        {
          if ( testSTC299b() != BAD )
          {
            Rip_STC299b ();
            break;
          }
        }


         /* StoneCracker 2.99 */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF0 ) &&
             (in_data[PW_i+5]  == 0x00 ) &&
             (in_data[PW_i+6]  == 0x4B ) &&
             (in_data[PW_i+7]  == 0xfa ) &&
             (in_data[PW_i+8]  == 0x01 ) &&
             (in_data[PW_i+9]  == 0x5c ) &&
             (in_data[PW_i+10] == 0x49 ) &&
             (in_data[PW_i+11] == 0xf9 ) &&
             (in_data[PW_i+12] == 0x00 ) &&
             (in_data[PW_i+13] == 0xbf ) &&
             (in_data[PW_i+14] == 0xd1 ) &&
             (in_data[PW_i+15] == 0x00 ) )
        {
          if ( testSTC299() != BAD )
          {
            Rip_STC299 ();
            break;
          }
        }

         /* StoneCracker 3.00 */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF0 ) &&
             (in_data[PW_i+5]  == 0x96 ) &&
             (in_data[PW_i+6]  == 0x4b ) &&
             (in_data[PW_i+7]  == 0xfa ) &&
             (in_data[PW_i+8]  == 0x01 ) &&
             (in_data[PW_i+9]  == 0x5c ) &&
             (in_data[PW_i+10] == 0x49 ) &&
             (in_data[PW_i+11] == 0xf9 ) &&
             (in_data[PW_i+12] == 0x00 ) &&
             (in_data[PW_i+13] == 0xbf ) &&
             (in_data[PW_i+14] == 0xd1 ) &&
             (in_data[PW_i+15] == 0x00 ) )
        {
          if ( testSTC300() != BAD )
          {
            Rip_STC300 ();
            break;
          }
        }

         /* StoneCracker 3.10 */
        if ( (in_data[PW_i+1]  == 0xF9 ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDF ) &&
             (in_data[PW_i+4]  == 0xF1 ) &&
             (in_data[PW_i+5]  == 0x80 ) &&
             (in_data[PW_i+6]  == 0x47 ) &&
             (in_data[PW_i+7]  == 0xf9 ) &&
             (in_data[PW_i+8]  == 0x00 ) &&
             (in_data[PW_i+9]  == 0xbf ) &&
             (in_data[PW_i+10] == 0xd1 ) &&
             (in_data[PW_i+11] == 0x00 ) &&
             (in_data[PW_i+12] == 0x41 ) &&
             (in_data[PW_i+13] == 0xfa ) &&
             (in_data[PW_i+14] == 0x00 ) &&
             (in_data[PW_i+15] == 0x62 ) )
        {
          if ( testSTC310() != BAD )
          {
            Rip_STC310 ();
            break;
          }
        }
#endif
        break;

      case 'P': /* 0x50 */
#ifdef INCLUDEALL
          /* "PP20" : ID of PowerPacker */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == '2') &&
             (in_data[PW_i+3] == '0') )
        {
          printf ( "PowerPacker ID (PP20) found at %ld ... cant rip it!\n" , PW_i );
          break;
        }
#endif
          /* "P30A" : ID of The Player */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'A') )
        {
	  if ( testP40A() == BAD ) /* yep same tests apply */
	    break;
	  Rip_P30A ();
          Depack_P30 ();
        }


          /* "P22A" : ID of The Player */
        if ( (in_data[PW_i+1] == '2') &&
             (in_data[PW_i+2] == '2') &&
             (in_data[PW_i+3] == 'A') )
        {
	  if ( testP40A() == BAD ) /* yep, same tests apply */
	    break;
	  Rip_P22A ();
          Depack_P22 ();
        }

          /* "P40A" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP40A() == BAD )
            break;
          Rip_P40A ();
          Depack_P40 ();
        }

          /* "P40B" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'B') )
        {
          if ( testP40A() == BAD )
            break;
          Rip_P40B ();
          Depack_P40 ();
        }

          /* "P41A" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP41A() == BAD )
            break;
          Rip_P41A ();
          Depack_P41A ();
          break;
        }

          /* "PM40" : ID of Promizer 4 */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == '4') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testPM40() == BAD )
            break;
          Rip_PM40 ();
          Depack_PM40 ();
          break;
        }

#ifdef INCLUDEALL
          /* "PPbk" : ID of AMOS PowerPacker Bank */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'b') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testPPbk() == BAD )
            break;
          Rip_PPbk ();
          break;
        }

          /* PARA data Cruncher */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "PARA Data Cruncher" , 46 , PARA );
          break;
        }

          /* Master cruncher 3.0 data */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == 'K') &&
             (in_data[PW_i+4] == 'V') &&
             (in_data[PW_i+5] == '1') &&
             (in_data[PW_i+6] == '.') &&
             (in_data[PW_i+7] == '2') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Master Cruncher 3.0 data" , 8 , MasterCruncher3data );
          break;
        }
#endif
          /* POLKA Packer */
        if ( ((in_data[PW_i+1] == 'W') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == '.')) ||
	     ((in_data[PW_i+1] == 'S') &&
             (in_data[PW_i+2] == 'U') &&
	     (in_data[PW_i+3] == 'X')))
        {
          if ( testPolka() == BAD )
            break;
          Rip_Polka ();
          Depack_Polka ();
          break;
        }
        break;

      case 'R':  /* RNC */
#ifdef INCLUDEALL
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'C') )
        {
          /* RNC */
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Propack (RNC) data" , 18 , RNC );
          break;
        }
          /* RLE Data Cruncher */
        if ( (in_data[PW_i+1] == 'L') &&
             (in_data[PW_i+2] == 'E') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "RLE Data Cruncher" , 11 , RLE );
          break;
        }
#endif
        break;

      case 'S':  /* 0x53 */
          /* "SNT!" ProRunner 2 */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testPRUN2() == BAD )
            break;
          Rip_PRUN2 ();
          Depack_PRUN2 ();
          break;
        }
          /* "SNT." ProRunner 1 */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '.') )
        {
          if ( testPRUN1() == BAD )
            break;
          Rip_PRUN1 ();
          Depack_PRUN1 ();
          break;
        }

          /* SKYT packer */
        if ( (in_data[PW_i+1] == 'K') &&
             (in_data[PW_i+2] == 'Y') &&
             (in_data[PW_i+3] == 'T') )
        {
          if ( testSKYT() == BAD )
            break;
          Rip_SKYT ();
          Depack_SKYT ();
          break;
        }

          /* SMOD Future Composer 1.0 - 1.3 */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testFC13() == BAD )
            break;
          Rip_FC13 ();
          break;
        }

#ifdef INCLUDEALL
          /* S404 StoneCracker 4.04 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "StoneCracker 4.04 data" , 18 , S404 );
          break;
        }

          /* S403 StoneCracker 4.03 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "StoneCracker 4.03 data" , 20 , S404 );
          break;
        }

          /* S401 StoneCracker 4.01 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '1') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "StoneCracker 4.01 data" , 12 , S404 );
          break;
        }

          /* S310 StoneCracker 3.10 data */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "StoneCracker 3.10 data" , 16 , S404 );
          break;
        }

          /* S300 StoneCracker 3.00 data */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "StoneCracker 3.00 data" , 16 , S404 );
          break;
        }

          /* SF data cruncher */
        /*if ( (in_data[PW_i+1] == 'F') )
        {
          if ( testSpecialCruncherData ( 6, 2 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SF Data Cruncher" , 11 , SF );
          break;
	  }*/

          /* SQ data cruncher */
	/*        if ( (in_data[PW_i+1] == 'Q') )
        {
          if ( testSpecialCruncherData ( 6, 2 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SQ Data Cruncher" , 999991 , SQ );
          break;
	  }*/

          /* SP data cruncher */
        /*if ( (in_data[PW_i+1] == 'P') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SP Data Cruncher" , 12 , SP );
          break;
	  }*/
#endif
          /* STIM Slamtilt */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'I') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testSTIM() != BAD )
          {
            Rip_STIM ();
            Depack_STIM ();
            break;
          }
        }

#ifdef INCLUDEALL
	  /* SPv3 - Speed Packer 3 (Atari ST) */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'v') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 8, 12 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Speed Packer 3 data" , 0 , SpeedPacker3Data );
          break;
        }
#endif
          /* SONG Fuchs Tracker */
        if ( (in_data[PW_i+1] == 'O') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == 'G') )
        {
          if ( testFuchsTracker() != BAD )
          {
            Rip_FuchsTracker ();
            Depack_FuchsTracker ();
            break;
          }
          /* Sound FX */
          if ( testSoundFX13() != BAD )
          {
            Rip_SoundFX13 ();
#ifndef UNIX
            Depack_SoundFX13 ();
#endif
            break;
          }
        }
        break;

      case 'U': /* "UNIC" */
        if ( ( in_data[PW_i+1] == 'N' ) &&
             ( in_data[PW_i+2] == 'I' ) &&
             ( in_data[PW_i+3] == 'C' ) )
        {
          /* Unic tracker v1 ? */
          if ( testUNIC_withID() != BAD )
          {
            Rip_UNIC_withID ();
            Depack_UNIC ();
            break;
          }
        }
	/* Mugician */
        if ( ( in_data[PW_i+1] == 'G' ) &&
             ( in_data[PW_i+2] == 'I' ) &&
             ( in_data[PW_i+3] == 'C' ) )
        {
          /*  */
          if ( testMUGICIAN() != BAD )
          {
            Rip_MUGICIAN ();
            break;
          }
        }
        break;

      case 'V': /* "V.2" */
        if ( (( in_data[PW_i+1] == '.' ) &&
              ( in_data[PW_i+2] == '2' )) ||
             (( in_data[PW_i+1] == '.' ) &&
              ( in_data[PW_i+2] == '3' )) )
        {
          /* Sound Monitor v2/v3 */
          if ( testBP() == BAD )
            break;
          Rip_BP ();
          break;
        }
#ifdef INCLUDEALL
          /* Virtual Dreams VDCO data cruncher */
        if ( (in_data[PW_i+1] == 'D') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == 'O') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "Virtual Dreams (VDCO) data cruncher" , 13 , VDCO );
          break;
        }
#endif
        break;

      case 'T':
          /* "TRK1" Module Protector */
        if ( ( in_data[PW_i+1] == 'R' ) &&
             ( in_data[PW_i+2] == 'K' ) &&
             ( in_data[PW_i+3] == '1' ) )
        {
          /* Module Protector */
          if ( testMP_withID() == BAD )
            break;
          Rip_MP_withID ();
          Depack_MP ();
          break;
        }

	  /* "TMK. Timetracker ?!? */
        if ( ( in_data[PW_i+1] == 'M' ) &&
             ( in_data[PW_i+2] == 'K' ) &&
             ( in_data[PW_i+3] == 0x01 ) )
        {
          if ( testTMK() == BAD )
            break;
          Rip_TMK ();
          Depack_TMK ();
          break;
        }
        break;

      case 'W': /* 0x57 */
          /* "WN" Wanton Packer */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 0x00 ) )
        {
          if ( testWN() == BAD )
            break;
          Rip_WN ();
          Depack_WN ();
          break;
        }
        break;

      case 'X': /* XPKF */
#ifdef INCLUDEALL
          /* xpk'ed file */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'K') &&
             (in_data[PW_i+3] == 'F') )
        {
          if ( testSpecialCruncherData ( 4, 12 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "XPK" , 8 , XPK );
          break;
        }
#endif
        break;

      case 0x60:
          /* promizer 1.8a ? */
        if ( (in_data[PW_i+1] == 0x38) &&
             (in_data[PW_i+2] == 0x60) &&
             (in_data[PW_i+3] == 0x00) &&
             (in_data[PW_i+4] == 0x00) &&
             (in_data[PW_i+5] == 0xa0) &&
             (in_data[PW_i+6] == 0x60) &&
             (in_data[PW_i+7] == 0x00) &&
             (in_data[PW_i+8] == 0x01) &&
             (in_data[PW_i+9] == 0x3e) &&
             (in_data[PW_i+10]== 0x60) &&
             (in_data[PW_i+11]== 0x00) &&
             (in_data[PW_i+12]== 0x01) &&
             (in_data[PW_i+13]== 0x0c) &&
             (in_data[PW_i+14]== 0x48) &&
             (in_data[PW_i+15]== 0xe7) )   /* gosh !, should be enough :) */
        {
          if ( testPMZ() != BAD )
          {
            Rip_PM18a ();
            Depack_PM18a ();
            break;
          }

          /* Promizer 1.0c */
          if ( testPM10c() != BAD )
          {
            Rip_PM10c ();
            Depack_PM10c ();
            break;
          }
          break;
        }

          /* promizer 2.0 ? */
        if ( (in_data[PW_i+1] == 0x00) &&
             (in_data[PW_i+2] == 0x00) &&
             (in_data[PW_i+3] == 0x16) &&
             (in_data[PW_i+4] == 0x60) &&
             (in_data[PW_i+5] == 0x00) &&
             (in_data[PW_i+6] == 0x01) &&
             (in_data[PW_i+7] == 0x40) &&
             (in_data[PW_i+8] == 0x60) &&
             (in_data[PW_i+9] == 0x00) &&
             (in_data[PW_i+10]== 0x00) &&
             (in_data[PW_i+11]== 0xf0) &&
             (in_data[PW_i+12]== 0x3f) &&
             (in_data[PW_i+13]== 0x00) &&
             (in_data[PW_i+14]== 0x10) &&
             (in_data[PW_i+15]== 0x3a) )   /* gosh !, should be enough :) */
        {
          if ( testPM2() == BAD )
            break;
          Rip_PM20 ();
          Depack_PM20 ();
          break;
        }

#ifdef INCLUDEALL
          /* Spike Cruncher */
        if ( (in_data[PW_i+1]  == 0x16) &&
             (in_data[PW_i+24] == 0x48) &&
             (in_data[PW_i+25] == 0xE7) &&
             (in_data[PW_i+26] == 0xFF) &&
             (in_data[PW_i+27] == 0xFE) &&
             (in_data[PW_i+28] == 0x26) &&
             (in_data[PW_i+29] == 0x7A) &&
             (in_data[PW_i+30] == 0xFF) &&
             (in_data[PW_i+31] == 0xDE) &&
             (in_data[PW_i+32] == 0xD7) &&
             (in_data[PW_i+33] == 0xCB) &&
             (in_data[PW_i+34] == 0xD7) &&
             (in_data[PW_i+35] == 0xCB) &&
             (in_data[PW_i+36] == 0x58) &&
             (in_data[PW_i+37] == 0x8B) )
        {
          if ( testSpikeCruncher() == BAD )
            break;
          Rip_SpikeCruncher ();
          break;
        }
#endif
        break;

      case 0x61: /* "a" */
#ifdef INCLUDEALL
         /* TNM Cruncher 1.1 */
        if ( (in_data[PW_i+1]  == 0x06) &&
             (in_data[PW_i+2]  == 0x4E) &&
             (in_data[PW_i+3]  == 0xF9) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0x00) &&
             (in_data[PW_i+6]  == 0x00) &&
             (in_data[PW_i+7]  == 0x00) &&
             (in_data[PW_i+8]  == 0x48) &&
             (in_data[PW_i+9]  == 0xE7) &&
             (in_data[PW_i+10] == 0xFF) &&
             (in_data[PW_i+11] == 0xFE) &&
             (in_data[PW_i+12] == 0x2C) &&
             (in_data[PW_i+13] == 0x78) &&
             (in_data[PW_i+14] == 0x00) &&
             (in_data[PW_i+15] == 0x04) )
        {
          if ( testTNMCruncher11() == BAD )
            break;
          Rip_TNMCruncher11 ();
          break;
        }

          /* "arcD" data cruncher */
        if ( (in_data[PW_i+1]  == 'r') &&
             (in_data[PW_i+2]  == 'c') &&
             (in_data[PW_i+3]  == 'D'))
        {
          if ( testArcDDataCruncher() == BAD )
            break;
          Rip_SpecialCruncherData ( "arcD data Cruncher" , 0 , arcD );
          break;
        }

        /* Tetrapack 2.1 */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x41) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0xE4) &&
             (in_data[PW_i+6]  == 0x4B) &&
             (in_data[PW_i+7]  == 0xF9) &&
             (in_data[PW_i+8]  == 0x00) &&
             (in_data[PW_i+9]  == 0xDF) &&
             (in_data[PW_i+10] == 0xF1) &&
             (in_data[PW_i+12] == 0xD1) &&
             (in_data[PW_i+13] == 0xFC) )
        {
          if ( testTetrapack_2_1() == BAD )
            break;
          Rip_Tetrapack_2_1 ();
          break;
        }

        /* Tetrapack 2.2 */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x43) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0xFC) &&
             (in_data[PW_i+12] == 0x28) &&
             (in_data[PW_i+13] == 0x7A) )
        {
          if ( testTetrapack_2_2() == GOOD )
            Rip_Tetrapack_2_2 ();
          break;
        }
#endif
        break;

      case 0x7E:
#ifdef INCLUDEALL
        /* Tetrapack 2.2 case #2 */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x43) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0xFC) &&
             (in_data[PW_i+12] == 0x28) &&
             (in_data[PW_i+13] == 0x7A) )
        {
          if ( testTetrapack_2_2() == GOOD )
            Rip_Tetrapack_2_2 ();
          break;
        }

        /* Tetrapack 2.1 */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x41) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0xE4) &&
             (in_data[PW_i+6]  == 0x4B) &&
             (in_data[PW_i+7]  == 0xF9) &&
             (in_data[PW_i+8]  == 0x00) &&
             (in_data[PW_i+9]  == 0xDF) &&
             (in_data[PW_i+10] == 0xF1))
        {
          if ( testTetrapack_2_1() == BAD )
            break;
          Rip_Tetrapack_2_1 ();
          break;
        }

        /* Defjam Cruncher 3.2T */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x43) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x02) &&
             (in_data[PW_i+5]  == 0x8C) &&
             (in_data[PW_i+6]  == 0x4B) &&
             (in_data[PW_i+7]  == 0xF9) &&
             (in_data[PW_i+8]  == 0x00) &&
             (in_data[PW_i+9]  == 0xDF) &&
             (in_data[PW_i+10] == 0xF1))
        {
          if ( testDefjam32t() == BAD )
            break;
          Rip_Defjam32 ();
          break;
        }
#endif
	break;
     case 0x80:
          /* Viruz2 8000 */
/*     if ( (in_data[PW_i+1] == 0x00) &&
          (in_data[PW_i+2] == 0x00) &&
          (in_data[PW_i+3] == 0x00) &&
          ((in_data[PW_i+6] == 0x00) || (in_data[PW_i+8] == 0x01)) &&
          (in_data[PW_i+7] == 0x00) &&
          (in_data[PW_i+8] == 0x01) &&
          (in_data[PW_i+9] == 0x00) &&
          (in_data[PW_i+10] == 0x00) &&
          (in_data[PW_i+11] == 0x00) &&
          (in_data[PW_i+12] == 0x00) &&
          (in_data[PW_i+13] == 0x00) &&
          (in_data[PW_i+14] == 0x00) &&
          (in_data[PW_i+16] == 0xAB) &&
          (in_data[PW_i+17] == 0x05) &&
          (in_data[PW_i+18] == 0x00) &&
          (in_data[PW_i+19] == 0x04) &&
          (in_data[PW_i+20] == 0x04) &&
          (in_data[PW_i+21] == 0x08) )
       {
         if ( testViruz2_80 () == BAD )
           break;
         continue;
       }*/
       break;

      case 0xAC:
          /* AC1D packer ?!? */
        if ( in_data[PW_i+1] == 0x1D )
        {
          if ( testAC1D() != BAD )
          {
            Rip_AC1D ();
            Depack_AC1D ();
            break;
          }
        }
        break;

      case 0xC0:
          /* Pha Packer */
        if ( (PW_i >= 1) && (in_data[PW_i-1] == 0x03) )
        {
          if ( testPHA() != BAD )
          {
            Rip_PHA ();
            Depack_PHA ();
            break;
          }
        }
        break;

      case 0xE0:
          
/*        if ( (in_data[PW_i+1] == 0x01) &&
             (in_data[PW_i+2] == 0x00) &&
             (in_data[PW_i+3] == 0x00) &&
             (in_data[PW_i+7] == 0x00) &&
             (in_data[PW_i+8] == 0x01) &&
             (in_data[PW_i+9] == 0x00) &&
             (in_data[PW_i+10] == 0x00) &&
             (in_data[PW_i+11] == 0x00) &&
             (in_data[PW_i+12] == 0x00) )
        {
          if ( testViruz2_E0 () == BAD )
            break;
          continue;
        }*/
        break;


      default: /* do nothing ... save continuing :) */
        break;

    } /* end of switch */
  }
#if 0
  free ( in_data );
  printf ( "\n" );
  printf ( " 1997-2007 (c) Sylvain \"Asle\" Chipaux (asle@free.fr)\n\n");
/*  getchar();*/
  exit ( 0 );
#endif
  return 0;
}
