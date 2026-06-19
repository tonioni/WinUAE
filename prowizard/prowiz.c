/*
 * Pro-Wizard_1.c
 *
 * 1997-2016 (c) Sylvain "Asle" Chipaux
 *
*/

#include "globals.h"
#include "extern.h"
#include "vars.h"


#if 0
int main ( int ac , char **av )
#else
int prowizard_search (uint8_t *in_data_p, int PW_in_size_p)
#endif
{

#if 0
  printf ( "\n\n-<([ Pro-Wizard v1.70a ])>-\n\n" );

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
  printf ( "input file size : %d\n" , PW_in_size );
  if ( PW_in_size < MINIMAL_FILE_LENGHT )
  {
    printf ( "! input file size is too small ...\n" );
    fclose ( PW_in );
    exit ( 1 );
  }

  /* alloc mem */
  in_data = (uint8_t *) malloc ( PW_in_size );
  if ( in_data == NULL )
  {
    perror ( "Couldn't allocate memory" );
    exit ( 0 );
  }
  fread ( in_data , PW_in_size , 1 , PW_in );
  fclose ( PW_in );
#else
  Support_Types_FileDefault ();
  in_data = in_data_p;
  PW_in_size = PW_in_size_p;
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
        }
      }
      */
#ifdef INCLUDEALL
      /* StoneCracker 2.92 data (ex-$08090A08 data cruncher) */
      if ( (in_data[PW_i]   == 0x08) &&
           (in_data[PW_i+1] == 0x09) &&
           (in_data[PW_i+2] == 0x0A) &&
          ((in_data[PW_i+3] == 0x08) || 
           (in_data[PW_i+3] == 0x0A) ||
           (in_data[PW_i+3] == 0x0B) ||
           (in_data[PW_i+3] == 0x0E) ||
           (in_data[PW_i+3] == 0x0D)))
      {
        if ( testSpecialCruncherData ( 8, 4 ) != BAD )
        {
          Rip_SpecialCruncherData ( "StoneCracker 2.92 Data Cruncher" , 12 , STC292data );
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
        }
      }

      /* B9AB data cruncher */
      if ( (in_data[PW_i]   == 0x0B) &&
           (in_data[PW_i+1] == 0x09) &&
           (in_data[PW_i+2] == 0x0A) &&
           (in_data[PW_i+3] == 0x0B) )
      {
        if ( testB9AB () != BAD )
        {
          Rip_SpecialCruncherData ( "B9AB Data Cruncher" , 274, B9AB );
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
        }
      }

      /* -GD- GnoiPacker (Skizzo)*/
      if ( (in_data[PW_i]   == 0x2D) &&
           (in_data[PW_i+1] == 'G') &&
           (in_data[PW_i+2] == 'D') &&
           (in_data[PW_i+3] == 0x2D) )
      {
        if ( testSkizzo() != BAD )
        {
          Rip_Skizzo ();
          Depack_Skizzo ();
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
        if ( testMaxPacker12() != BAD )
        {
          Rip_MaxPacker12 ();
        }
      }
#endif

      /* XANN packer */
      if ( in_data[PW_i] == 0x3c )
      {
        if ( testXANN() != BAD )
        {
          Rip_XANN ();
          Depack_XANN ();
        }
      }

      /* hum ... that's where things become interresting :) */
      /* Module Protector without ID */
      /* LEAVE IT THERE !!! ... at least before Heatseeker format since they are VERY similare ! */
      if ( testMP_noID() != BAD )
      {
        Rip_MP_noID ();
        Depack_MP ();
      }

      /* Digital Illusion */
      if ( testDI() != BAD )
      {
        Rip_DI ();
        Depack_DI ();
      }

      /* SGTPacker */
      /*
      if ( testSGT() != BAD )
      {
        Rip_SGT ();
        Depack_SGT ();
      }
      */
      /* eureka packer */
      if ( testEUREKA() != BAD )
      {
        Rip_EUREKA ();
        Depack_EUREKA ();
      }

      /* The player 5.0a ? */
      if ( testP50A() != BAD )
      {
        Rip_P50A ();
        Depack_P50A ();
      }

      /* The player 6.0a ? */
      if ( testP60A_nopack() != BAD )
      {
        Rip_P60A ();
        Depack_P60A ();
      }

      /* The player 6.0a (packed samples)? */
      if ( testP60A_pack() != BAD )
      {
        printf ( "\b\b\b\b\b\b\b\bThe Player 6.0A with PACKED samples found at %d ... cant rip it!\n" , PW_Start_Address );
        /*Rip_P60A ();*/
        /*Depack_P60A ();*/
      }

      /* The player 6.1a ? */
      if ( testP61A_nopack() != BAD )
      {
        Rip_P61A ();
        Depack_P61A ();
      }

      /* The player 6.1a (packed samples)? */
      if ( testP61A_pack() != BAD )
      {
        printf ( "\b\b\b\b\b\b\b\bThe Player 6.1A with PACKED samples found at %d ... cant rip it!\n" , PW_Start_Address );
        /*Rip_P61A ();*/
        /*Depack_P61A ();*/
      }

      /* Propacker 1.0 */
      if ( testPP10() != BAD )
      {
        Rip_PP10 ();
        Depack_PP10 ();
      }

      /* Noise Packer v2 */
      /* LEAVE VERSION 2 BEFORE VERSION 1 !!!!! */
      if ( testNoisepacker2() != BAD )
      {
        Rip_Noisepacker2 ();
        Depack_Noisepacker2 ();
      }

      /* Noise Packer v1 */
      if ( testNoisepacker1() != BAD )
      {
        Rip_Noisepacker1 ();
        Depack_Noisepacker1 ();
      }

      /* Noise Packer v3 */
      if ( testNoisepacker3() != BAD )
      {
        Rip_Noisepacker3 ();
        Depack_Noisepacker3 ();
      }

      /* Promizer 0.1 */
      if ( testPM01() != BAD )
      {
        Rip_PM01 ();
        Depack_PM01 ();
      }

      /* ProPacker 2.1 */
      if ( testPP21() != BAD )
      {
        Rip_PP21 ();
        Depack_PP21 ();
      }

      /* ProPacker 3.0 */
      if ( testPP30() != BAD )
      {
        Rip_PP30 ();
        Depack_PP30 ();
      }

      /* StartTrekker pack */
      if ( testSTARPACK() != BAD )
      {
        Rip_STARPACK ();
        Depack_STARPACK ();
      }

      /* Zen packer */
      if ( testZEN() != BAD )
      {
        Rip_ZEN ();
        Depack_ZEN ();
      }

      /* Unic tracker v1 ? */
      if ( testUNIC_withemptyID() != BAD )
      {
        Rip_UNIC_withID ();
        Depack_UNIC ();
      }

      /* Unic tracker v1 ? */
      if ( testUNIC_noID() != BAD )
      {
        Rip_UNIC_noID ();
        Depack_UNIC ();
      }

      /* Unic trecker v2 ? */
      if ( testUNIC2() != BAD )
      {
        Rip_UNIC2 ();
        Depack_UNIC2 ();
      }

      /* Game Music Creator ? */
      if ( testGMC() != BAD )
      {
        Rip_GMC ();
        Depack_GMC ();
      }

      /* Heatseeker ? */
      if ( testHEATSEEKER() != BAD )
      {
        Rip_HEATSEEKER ();
        Depack_HEATSEEKER ();
      }

      /* SoundTracker (15 smp) */
      if ( testSoundTracker() != BAD )
      {
        Rip_SoundTracker ();
      }

      /* The Dark Demon (group name) format */
      if ( testTheDarkDemon() != BAD )
      {
        Rip_TheDarkDemon ();
        Depack_TheDarkDemon ();
      }

      /* Newtron */
      if ( testNewtron() != BAD )
      {
        Rip_Newtron ();
        Depack_Newtron ();
      }

      /* Newtron Old */
      if ( testNewtronOld() != BAD )
      {
        Rip_NewtronOld ();
        Depack_NewtronOld ();
      }

      /* Titanics Player ? */
      if ( testTitanicsPlayer() != BAD )
      {
        Rip_TitanicsPlayer ();
        Depack_TitanicsPlayer ();
      }

      /* Struggle game ? */
/*      if ( testSTRUGGLE() != BAD )
      {
        Rip_STRUGGLE ();
        Depack_STRUGGLE ();
        continue;
      }*/
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
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          }
        }
#endif
	/* AMOS Music bank "AmBk" */
        if ( (in_data[PW_i+1] == 'm') &&
             (in_data[PW_i+2] == 'B') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testAmBk() != BAD )
          {
            Rip_AmBk();
            Depack_AmBk();
          }
        }
	/* Sidmon v1 */
        if ( in_data[PW_i+1] == 0xFA )
        {
          if ( testSIDMON1() != BAD )
          {
            Rip_SIDMON1();
          }
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
          if ( testTimeCruncher17() != BAD )
          {
            Rip_TimeCruncher17 ();
          }
        }
        /* IAM Cruncher 1.0 (another case (aka ICE)) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'M') &&
             (in_data[PW_i+3] == '5') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "IAM Packer 1.0 (ATM5) data" , 12 , ICE );
          }
        }
        /* ATOM - Atomik Packer (Atari ST) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Atomik Packer (ATOM) data" , 12 , AtomikPackerData );
          }
        }
        /* ATM3 - Atomik Packer (Atari ST) */
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'M') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Atomik Packer (ATM3) data" , 12 , AtomikPackerData );
          }
        }
          /* "AU5!" - Automation Packer 5.* (Atari ST) */
        if ( (in_data[PW_i+1] == 'U') &&
             (in_data[PW_i+2] == '5') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Automation Packer v5.01 (data)" , 0 , AutomationPackerData );
          }
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
          }
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
          if ( testTetrapack102() != BAD )
          {
            Rip_Tetrapack102 ();
          }
        }
        /* Tetrapack 1.01 */
        if ( (in_data[PW_i+1]  == 0xFA ) &&
             (in_data[PW_i+2]  == 0x00 ) &&
             (in_data[PW_i+3]  == 0xDE ) &&
             (in_data[PW_i+4]  == 0xD1 ) &&
             (in_data[PW_i+5]  == 0xFC ) &&
             (in_data[PW_i+10] == 0x22 ) &&
             (in_data[PW_i+11] == 0x7C ) &&
             (in_data[PW_i+16] == 0x24 ) &&
             (in_data[PW_i+17] == 0x60 ) &&
             (in_data[PW_i+18] == 0xD5 ) &&
             (in_data[PW_i+19] == 0xC9 ) )
        {
          if ( testTetrapack101() != BAD )
          {
            Rip_Tetrapack101 ();
          }
        }
        /* "ArcD" data cruncher */
        if ( (in_data[PW_i+1]  == 'r') &&
             (in_data[PW_i+2]  == 'c') &&
             (in_data[PW_i+3]  == 'D'))
        {
          if ( testArcDDataCruncher() != BAD )
          {
            Rip_SpecialCruncherData ( "ArcD data Cruncher" , 0 , arcD );
          }
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
          }
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
          if ( testbytekillerpro10() != BAD )
          {
            Rip_bytekillerpro10 ();
          }
        }
        /* Bytekiller Clone FLT */
        if ( (in_data[PW_i+1] == 0xFA) &&
             (in_data[PW_i+2] == 0x01) &&
             (in_data[PW_i+3] == 0x32) &&
             (in_data[PW_i+4] == 0xD1) &&
             (in_data[PW_i+5] == 0xFA) &&
             (in_data[PW_i+6] == 0x01) &&
             (in_data[PW_i+7] == 0x2A) &&
             (in_data[PW_i+8] == 0x22) &&
             (in_data[PW_i+9] == 0x7c) )
        {
          if ( testBKCloneFLT() != BAD )
          {
            Rip_BKCloneFLT ();
          }
        }
        /* unknown Bytekiller Clone 5 */
        if ( (in_data[PW_i+1] == 0xFA) &&
             (in_data[PW_i+2] == 0x01) &&
             (in_data[PW_i+3] == 0x16) &&
             (in_data[PW_i+4] == 0xD1) &&
             (in_data[PW_i+5] == 0xFC) &&
             (in_data[PW_i+10] == 0x43) &&
             (in_data[PW_i+11] == 0xF9) &&
             (in_data[PW_i+16] == 0x24) &&
             (in_data[PW_i+17] == 0x60) )
        {
          if ( testBKClone5() != BAD )
          {
            Rip_BKClone5 ();
          }
        }
	/* Ace? (data cruncher) */
        if ( (in_data[PW_i+1] == 'c') &&
             (in_data[PW_i+2] == 'e') &&
             (in_data[PW_i+3] == '?') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "(Ace?) Data Cruncher" , 0 , ACECruncherData );
          }
        }
#endif
        /* Delta Music 1 */
        if ( (in_data[PW_i+1] == 'L') &&
             (in_data[PW_i+2] == 'L') &&
             (in_data[PW_i+3] == ' ') )
        {
          if ( testDM1() != BAD )
          {
            Rip_DM1();
          }
        }


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
          }
          if ( testByteKiller_20() != BAD )
          {
            Rip_ByteKiller ();
          }
        }

        /* BHC2 (data cruncher) */
        if ( (in_data[PW_i+1] == 'H') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == '2') )
        {
          if ( testSpecialCruncherData ( 6, 10 ) != BAD )
          {
            Rip_SpecialCruncherData ( "(BHC2) Data Cruncher" , 0x3C , BHC2CruncherData );
          }
        }
        /* BHC3 (data cruncher) */
        if ( (in_data[PW_i+1] == 'H') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "(BHC3) Data Cruncher" , 0xC8 , BHC3CruncherData );
          }
        }
#endif
        /* "BeEp" Jam Cracker */
        if ( (in_data[PW_i+1] == 'e') &&
             (in_data[PW_i+2] == 'E') &&
             (in_data[PW_i+3] == 'p') )
        {
          if ( testJamCracker() != BAD )
          {
            Rip_JamCracker ();
          }
        }

        /* "BNR!" Binary Packer */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testBNR() != BAD )
          {
            Rip_BNR();
            Depack_BNR();
          }
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
          if ( testTP3() != BAD )
          {
            Rip_TP3 ();
            Depack_TP3 ();
          }
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
          if ( testSpecialCruncherData ( 10, 6 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Crunchmania / Normal data" , 14 , CRM1 );
          }
        }
        /* "CHFI"  another imploder case */
        if ( (in_data[PW_i+1] == 'H') &&
             (in_data[PW_i+2] == 'F') &&
             (in_data[PW_i+3] == 'I') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          }
        }
        /* "CRND" data cruncher */
        if ( (in_data[PW_i+1] == 'R') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testCRND() != BAD )
          {
            Rip_SpecialCruncherData ( "CRND data cruncher" , 20 , CRND );
          }
        }
	/* Defjam Cruncher 3.2 */
        if ( (in_data[PW_i+1] == 0xFA) &&
             (in_data[PW_i+2] == 0x02) &&
             (in_data[PW_i+3] == 0x8C) &&
             ((in_data[PW_i+4] == 0x4B) || (in_data[PW_i+4] == 0x9B))&&
             ((in_data[PW_i+5] == 0xF9) || (in_data[PW_i+5] == 0xCD))&&
             ((in_data[PW_i+6] == 0x00) || (in_data[PW_i+6] == 0x4E)))
        {
          if ( testDefjam32() != BAD )
          {
            Rip_Defjam32 ();
          }
        }
#endif
        break;

      case 'D': /* 0x44 */
        /* Digibooster 1.7 */
        if ( (in_data[PW_i+1] == 'I') &&
	     (in_data[PW_i+2] == 'G') &&
	     (in_data[PW_i+3] == 'I') )
        {
          if ( testDigiBooster17() != BAD )
          {
            Rip_DigiBooster17 ();
          }
        }
        break;

      case 'E': /* 0x45 */
          /* "EMOD" : ID of Quadra Composer */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testQuadraComposer() != BAD )
          {
            Rip_QuadraComposer ();
            /*Depack_QuadraComposer ();*/
          }
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
          if ( testXM() != BAD )
          {
            Rip_XM ();
          }
        }
        break;

      case 'F': /* 0x46 */
          /* "FC-M" : ID of FC-M packer */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == '-') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testFC_M() != BAD )
          {
            Rip_FC_M ();
            Depack_FC_M ();
          }
        }
          /* "FLT4" : ID of StarTrekker */
        if ( (in_data[PW_i+1] == 'L') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testMOD(4) != BAD )
          {
            Rip_MOD (4);
          }
        }
          /* "FC14" : Future Composer 1.4 */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testFC14() != BAD )
          {
            Rip_FC14 ();
          }
        }
          /* "FORM" : EA-IFF */
        if ( (in_data[PW_i+1] == 'O') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testIFF() != BAD )
          {
            Rip_IFF ();
          }
        }
          /* "FUCO" : ID of BSI Future Composer */
        if ( (in_data[PW_i+1] == 'U') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == 'O') )
        {
          if ( testBSIFutureComposer() != BAD )
          {
            Rip_BSIFutureComposer ();
          }
        }
         /* "Fuck" : ID of Noise From Heaven chiptunes */
        if ( (in_data[PW_i+1] == 'u') &&
             (in_data[PW_i+2] == 'c') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testNFH() != BAD )
          {
            Rip_NFH ();
            Depack_NFH ();
          }
        }
    	  /* "FAST" : ID of Stone Arts Player */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'S') &&
             (in_data[PW_i+3] == 'T') )
        {
          if ( testStoneArtsPlayer() != BAD )
          {
            Rip_StoneArtsPlayer ();
  	        Depack_StoneArtsPlayer ();
          }
        }
#ifdef INCLUDEALL
          /* FIRE (RNC clone) Cruncher */
        if ( (in_data[PW_i+1] == 'I') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'E') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "FIRE (RNC Clone) data Cruncher" , 0 , FIRE );
          }
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
          }
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
          if ( testMegaCruncher12() != BAD )
	      {
            Rip_MegaCruncher ();
          }
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
          if ( testDoubleAction10() != BAD )
          {
            Rip_DoubleAction10 ();
          }
        }
#endif
	/* GPMO (crunch player ?)*/
        if ( (in_data[PW_i+1] == 'P') &&
           (in_data[PW_i+2] == 'M') &&
           (in_data[PW_i+3] == 'O') )
        {
          if ( testGPMO() != BAD )
          {
            Rip_GPMO ();
            Depack_GPMO ();
          }
        }

	/* Gnu player */
        if ( (in_data[PW_i+1] == 'n') &&
           (in_data[PW_i+2] == 'P') &&
           (in_data[PW_i+3] == 'l') )
        {
          if ( testGnuPlayer() != BAD )
          {
            Rip_GnuPlayer ();
            Depack_GnuPlayer ();
          }
        }

        break;

      case 'H': /* 0x48 */
          /* "HRT!" : ID of Hornet packer */
        if ( (in_data[PW_i+1] == 'R') &&
           (in_data[PW_i+2] == 'T') &&
           (in_data[PW_i+3] == '!') )
        {
          if ( testHRT() != BAD )
          {
            Rip_HRT ();
            Depack_HRT ();
          }
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
          if ( testMasterCruncher30addr() != BAD )
          {
            Rip_MasterCruncher30addr ();
          }
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
          if ( testPowerpacker4lib() != BAD )
          {
            Rip_Powerpacker4lib ();
          }
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
          if ( testStoneCracker270() != BAD )
          {
            Rip_StoneCracker270 ();
          }
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
          if ( testByteKiller30() != BAD )
          {
            Rip_ByteKiller30 ();
          }
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
          if ( testPowerpacker23() != BAD )
          {
            Rip_Powerpacker23 ();
          }
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
          if ( testPowerpacker30() != BAD )
          {
            Rip_Powerpacker30 ();
          }
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
          if ( testPowerpacker40() != BAD )
          {
            Rip_Powerpacker40 ();
          }
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
          if ( testSuperCruncher27() != BAD )
          {
            Rip_SuperCruncher27 ();
          }
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
          if ( testcrunchmaniaAddr(1) != BAD )
          {
            Rip_CrunchmaniaAddr ();
          }
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
          if ( testcrunchmaniaAddr(1) != BAD )
          {
            Rip_CrunchmaniaAddr ();
          }
        }

        /* Crunchmania Address (another again)*/
        if ((in_data[PW_i+1] == 0xe7) &&
           (in_data[PW_i+14] == 0x1a) &&
           (in_data[PW_i+15] == 0xbc) &&
           (in_data[PW_i+16] == 0x00) &&
           (in_data[PW_i+17] == 0xb9) &&
           (in_data[PW_i+18] == 0x1a) &&
           (in_data[PW_i+19] == 0xbc) &&
           (in_data[PW_i+24] == 0x00) &&
           (in_data[PW_i+25] == 0xe9) &&
           (in_data[PW_i+26] == 0x1a) &&
           (in_data[PW_i+27] == 0xbc) &&
           (in_data[PW_i+28] == 0x00) &&
           (in_data[PW_i+29] == 0xf1) &&
           (in_data[PW_i+30] == 0x45) &&
           (in_data[PW_i+31] == 0xfa) )
        {
          if ( testcrunchmaniaAddr(2) != BAD )
          {
            Rip_CrunchmaniaAddr ();
          }
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
          if ( testcrunchmaniaSimple() != BAD )
          {
            Rip_CrunchmaniaSimple();
          }
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
          if ( testRelokIt10() != BAD )
          {
            Rip_RelokIt10 ();
          }
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
          if ( testMegaCruncherObj() != BAD )
          {
            Rip_MegaCruncherObj ();
          }
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
          if ( testTurboSqueezer61() != BAD )
          {
            Rip_TurboSqueezer61 ();
          }
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
          if ( testDragpack252() != BAD )
          {
            Rip_Dragpack252 ();
          }
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
          if ( testDragpack100() != BAD )
          {
            Rip_Dragpack100 ();
          }
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
          if ( testGNUPacker12() != BAD )
          {
            Rip_GNUPacker12 ();
          }
        }
#endif
        /* "HVL" - Hively tracker */
        if ( ( in_data[PW_i+1] == 'V' ) &&
             ( in_data[PW_i+2] == 'L' ) &&
             (( in_data[PW_i+3] == 0x00 )||( in_data[PW_i+3] == 0x01 )) )
        {
          if ( testTHX() != BAD )
          {
            Rip_THX ();
          }
        }
        break;

      case 'I': /* 0x48 */
#ifdef INCLUDEALL
          /* "ICE!" : ID of IAM packer 1.0 */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == 'E') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "IAM Packer 1.0 (ICE!) data" , 0 , ICE );
          }
        }
          /* "Ice!" : ID of Ice! Cruncher */
        if ( (in_data[PW_i+1] == 'c') &&
             (in_data[PW_i+2] == 'e') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Ice! Cruncher (data)" , 0 , ICE );
          }
        }
          /* "IMP!" */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'P') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Imploder data" , 50 , IMP );
          }
        }
          /* "IMPM" : ID of Impulse Tracker */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'P') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testIT() != BAD )
          {
            Rip_IT ();
          }
        }
#endif
        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '0') )
        {
          /* Ice Tracker 1.0 */
          if ( testSTK26() != BAD )
          {
            Rip_STK26 ();
            Depack_STK26 ();
          }
        }
        break;

      case 'K': /* 0x4B */
          /* "KRIS" : ID of Chip Tracker */
        if ( (in_data[PW_i+1] == 'R') &&
             (in_data[PW_i+2] == 'I') &&
             (in_data[PW_i+3] == 'S') )
        {
          if ( testKRIS() != BAD )
          {
            Rip_KRIS ();
            Depack_KRIS ();
          }
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
          if ( testTryIt101() != BAD )
          {
            Rip_TryIt101 ();
          }
        }
#endif
        break;

      case 'L': /* 0x4C */
#ifdef INCLUDEALL
          /* "LSD!" : ID of Automation 2.3r packer (Atari ST) */
        if ( (in_data[PW_i+1] == 'S') &&
             (in_data[PW_i+2] == 'D') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Automation 2.3r Data Cruncher" , 4 , LSDDataCruncher );
          }
        }

          /* "LZH!" : ID of Jam Packer (LZH! compression) (Atari ST) */
        if ( (in_data[PW_i+1] == 'Z') &&
             (in_data[PW_i+2] == 'H') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Jam Packer Data Cruncher" , 12 , JamDataCruncher );
          }
        }
#endif
        break;

      case 'M': /* 0x4D */
        /* MASM data cruncher */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'S') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testSpecialCruncherData ( 4, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "MASM Data Cruncher" , 0 , MASMDataCruncher );
          }
        }

        if ( (in_data[PW_i+1] == '.') &&
             (in_data[PW_i+2] == 'K') &&
             (in_data[PW_i+3] == '.') )
        {
          /* protracker ? */
          if ( testMOD(4) != BAD )
          {
            Rip_MOD(4);
          }

          /* Unic tracker v1 ? */
          if ( testUNIC_withID() != BAD )
          {
            Rip_UNIC_withID ();
            Depack_UNIC ();
          }

          /* Noiserunner ? */
          if ( testNoiserunner() != BAD )
          {
            Rip_Noiserunner ();
            Depack_Noiserunner ();
          }

          /* Mosh packer ? */
          if ( testMOSH() != BAD )
          {
            Rip_MOSH ();
            Depack_MOSH ();
          }
          /* HCD-protector ? */
          if ( testHCD() != BAD )
          {
            Rip_HCD ();
            Depack_HCD ();
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
          }
          /* tracker packer v1 */
          if ( testTP1() != BAD )
          {
            Rip_TP1 ();
            Depack_TP1 ();
          }
        }

        if ( in_data[PW_i+1] == '.' )
        {
          /* Kefrens sound machine ? */
          if ( testKSM() != BAD )
          {
            Rip_KSM ();
            Depack_KSM ();
          }
        }

        if ( (in_data[PW_i+1] == 'O') &&
             (in_data[PW_i+2] == 'D') &&
             (in_data[PW_i+3] == 'U') )
        {
          /* NovoTrade */
          if ( testNovoTrade() != BAD )
          {
            Rip_NovoTrade ();
            Depack_NovoTrade ();
            continue;
          }
        }

        if ( (in_data[PW_i+1] == 'T') &&
             (in_data[PW_i+2] == 'N') &&
             (in_data[PW_i+3] == 0x00) )
        {
          /* SoundTracker 2.6 */
          if ( testSTK26() != BAD )
          {
            Rip_STK26 ();
            Depack_STK26 ();
          }
        }

        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'D') &&
             ((in_data[PW_i+3] == '0') ||
             (in_data[PW_i+3] == '1')) )/* || 
             (in_data[PW_i+3] == '2') ||
             (in_data[PW_i+3] == '3')) )*/
        {
          /* MED (MMD0) */
          if ( testMMD0() != BAD )
          {
            Rip_MMD0 ();
          }
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
          }
        }

          /* Mental Image Packer (data) */
        if ( (in_data[PW_i+1] == 'I') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Mental Image Packer" , 18 , MentalImage );
          }
        }
#endif
        break;

      case 'N': /* Sonic Arranger (no hunk) */
        if ( (in_data[PW_i+1] == 0xFA) &&
             (in_data[PW_i+2] == 0x00) &&
             (in_data[PW_i+4] == 'N') &&
             (in_data[PW_i+5] == 0xFA) &&
             (in_data[PW_i+8] == 'N') &&
             (in_data[PW_i+9] == 0xFA) &&
             (in_data[PW_i+12] == 'N') &&
             (in_data[PW_i+13] == 0xFA) &&
             (in_data[PW_i+16] == 'N') &&
             (in_data[PW_i+17] == 0xFA) &&
             (in_data[PW_i+20] == 'N') &&
             (in_data[PW_i+21] == 0xFA) &&
             (in_data[PW_i+24] == 'N') &&
             (in_data[PW_i+25] == 0xFA))
        {
          if ( testSA() != BAD )
          {
            Rip_SA ();
          }
        }
        break;

      case 'O': /* 0x4F */
          /* "OKTASONG" : ID of Oktalizer */
        if ( (in_data[PW_i+1] == 'K') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == 'A') &&
             (in_data[PW_i+4] == 'S') &&
             (in_data[PW_i+5] == 'O') &&
             (in_data[PW_i+6] == 'N') &&
             (in_data[PW_i+7] == 'G') &&
             (in_data[PW_i+8] == 'C') &&
             (in_data[PW_i+9] == 'M') &&
             (in_data[PW_i+10] == 'O') &&
             (in_data[PW_i+11] == 'D') )
        {
          if ( testOkta() != BAD )
          {
            Rip_Okta ();
          }
        }
        break;

      case 'P': /* 0x50 */
#ifdef INCLUDEALL
          /* "PP20" : ID of PowerPacker */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == '2') &&
             (in_data[PW_i+3] == '0') )
        {
          printf ( "PowerPacker ID (PP20) found at %u ... cant rip it!\n" , PW_i );
        }
#endif
          /* "P30A" : ID of The Player */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP40A() != BAD ) /* yep same tests apply */
          {
            Rip_P30A ();
            Depack_P30 ();
          }
        }

          /* "P22A" : ID of The Player */
        if ( (in_data[PW_i+1] == '2') &&
             (in_data[PW_i+2] == '2') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP40A() != BAD ) /* yep, same tests apply */
	      {
            Rip_P22A ();
            Depack_P22 ();
          }
        }

          /* "P40A" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP40A() != BAD )
          {
            Rip_P40A ();
            Depack_P40 ();
          }
        }

          /* "P40B" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == 'B') )
        {
          if ( testP40A() != BAD )
          {
            Rip_P40B ();
            Depack_P40 ();
          }
        }

          /* "P41A" : ID of The Player */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testP41A() != BAD )
          {
            Rip_P41A ();
            Depack_P41A ();
          }
        }

          /* "PMd3/PMD3 : ID of ?!? TSCC format - 8CHN */
        if ( (in_data[PW_i+1] == 'M') &&
             ((in_data[PW_i+2] == 'd') || (in_data[PW_i+2] == 'D')) &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testPMD3() != BAD ) /* yep, same tests apply */
	      {
            Rip_PMD3 ();
            Depack_PMD3 ();
          }
        }

          /* "PM40" : ID of Promizer 4 */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == '4') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testPM40() != BAD )
          {
            Rip_PM40 ();
            Depack_PM40 ();
          }
        }

#ifdef INCLUDEALL
          /* "PPbk" : ID of AMOS PowerPacker Bank */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'b') &&
             (in_data[PW_i+3] == 'k') )
        {
          if ( testPPbk() != BAD )
          {
            Rip_PPbk ();
          }
        }

          /* PARA data Cruncher */
        if ( (in_data[PW_i+1] == 'A') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'A') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "PARA Data Cruncher" , 46 , PARA );
          }
        }

          /* Pac1 data Cruncher */
        if ( (in_data[PW_i+1] == 'a') &&
             (in_data[PW_i+2] == 'c') &&
             (in_data[PW_i+3] == '1') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Pac1 Data Cruncher" , 12 , Pac1 );
          }
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
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Master Cruncher 3.0 data" , 8 , MasterCruncher3data );
          }
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
          if ( testPolka() != BAD )
          {
            Rip_Polka ();
            Depack_Polka ();
          }
        }

          /* PERFSONG Packer */
        if ( (in_data[PW_i+1] == 'E') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'F') &&
             (in_data[PW_i+4] == 'S') &&
             (in_data[PW_i+5] == 'O') &&
             (in_data[PW_i+6] == 'N') &&
             (in_data[PW_i+7] == 'G'))
        {
          if ( testPERFSONG() != BAD )
          {
            Rip_PERFSONG ();
            Depack_PERFSONG ();
          }
        }
        break;

      case 'R':  /* RNC */
#ifdef INCLUDEALL
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'C') )
        {
          /* RNC */
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Propack (RNC) data" , 18 , RNC );
          }
        }
          /* RLE Data Cruncher */
        if ( (in_data[PW_i+1] == 'L') &&
             (in_data[PW_i+2] == 'E') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "RLE Data Cruncher" , 11 , RLE );
          }
        }
#endif
        break;

      case 'S':  /* 0x53 */
#ifdef INCLUDEALL
          /* SpeedPacker (SP20) Data Cruncher */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == '2') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 8, 12 ) != BAD )
          {
            Rip_SpecialCruncherData ( "SpeedPacker (SP20) Data Cruncher" , 16 , SP20 );
          }
        }
#endif
          /* "SNT!" ProRunner 2 */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '!') )
        {
          if ( testPRUN2() != BAD )
          {
            Rip_PRUN2 ();
            Depack_PRUN2 ();
          }
        }
          /* "SNT." ProRunner 1 */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 'T') &&
             (in_data[PW_i+3] == '.') )
        {
          if ( testPRUN1() != BAD )
          {
            Rip_PRUN1 ();
            Depack_PRUN1 ();
          }
        }

          /* SKYT packer */
        if ( (in_data[PW_i+1] == 'K') &&
             (in_data[PW_i+2] == 'Y') &&
             (in_data[PW_i+3] == 'T') )
        {
          if ( testSKYT() != BAD )
          {
            Rip_SKYT ();
            Depack_SKYT ();
          }
        }

          /* SMOD Future Composer 1.0 - 1.3 */
        if ( (in_data[PW_i+1] == 'M') &&
             (in_data[PW_i+2] == 'O') &&
             (in_data[PW_i+3] == 'D') )
        {
          if ( testFC13() != BAD )
          {
            Rip_FC13 ();
          }
        }

         /* SIDMON 2 */
/*        if ( (in_data[PW_i+1] == 'I') &&
             (in_data[PW_i+2] == 'D') &&
             (in_data[PW_i+3] == 'M') &&
             (in_data[PW_i+4] == 'O') &&
             (in_data[PW_i+5] == 'N') &&
             (in_data[PW_i+6] == ' ') &&
             (in_data[PW_i+7] == 'I') &&
             (in_data[PW_i+8] == 'I') &&
             (in_data[PW_i+9] == ' ') )
        {
          if ( testSIDMON2() != BAD )
          {
            Rip_SIDMON2 ();
          }
        }*/

#ifdef INCLUDEALL
          /* S404 StoneCracker 4.04 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '4') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 4.04 data" , 18 , S404 );
          }
        }

          /* S403 StoneCracker 4.03 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 4.03 data" , 20 , S404 );
          }
        }

          /* S401 StoneCracker 4.01 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '1') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 4.01 data" , 12 , S404 );
          }
        }
          /* S400 StoneCracker 4.00 data */
        if ( (in_data[PW_i+1] == '4') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 4.00 data" , 12 , S404 );
          }
        }

          /* S310 StoneCracker 3.10 data */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '1') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 3.10 data" , 16 , S404 );
          }
        }

          /* S300 StoneCracker 3.00 data */
        if ( (in_data[PW_i+1] == '3') &&
             (in_data[PW_i+2] == '0') &&
             (in_data[PW_i+3] == '0') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "StoneCracker 3.00 data" , 16 , S404 );
          }
        }

          /* "SCRM" : ID of ScreamTracker 3 S3M */
        if ( (in_data[PW_i+1] == 'C') &&
             (in_data[PW_i+2] == 'R') &&
             (in_data[PW_i+3] == 'M') )
        {
          if ( testS3M() != BAD )
          {
            Rip_S3M ();
          }
        }

          /* SC data cruncher */
/*        if ( (in_data[PW_i+1] == 'A') )
        {
          if ( testSpecialCruncherData ( 10, 6 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SA Data Cruncher" , 182 , SF );
          break;
        }*/

          /* SC data cruncher */
/*        if ( (in_data[PW_i+1] == 'C') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SC Data Cruncher" , 76 , SF );
          break;
        }*/

          /* SF data cruncher */
/*        if ( (in_data[PW_i+1] == 'F') )
        {
          if ( testSpecialCruncherData ( 6, 2 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SF Data Cruncher" , 11 , SF );
          break;
        }*/

          /* SQ data cruncher */
/*	    if ( (in_data[PW_i+1] == 'Q') )
        {
          if ( testSpecialCruncherData ( 6, 2 ) == BAD )
            break;
          Rip_SpecialCruncherData ( "SQ Data Cruncher" , 999991 , SQ );
          break;
	    }*/

          /* SP data cruncher */
/*        if ( (in_data[PW_i+1] == 'P') )
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
          }
        }

        /* SA hunk */
        if ( (in_data[PW_i+1] == 'O') &&
             (in_data[PW_i+2] == 'A') &&
             (in_data[PW_i+3] == 'R') &&
             (in_data[PW_i+4] == 'V') &&
             (in_data[PW_i+5] == '1') &&
             (in_data[PW_i+6] == '.') &&
             (in_data[PW_i+7] == '0') )
        {
          if ( testSAhunk() != BAD )
          {
            Rip_SA();
          }
        }

#ifdef INCLUDEALL
	  /* SPv3 - Speed Packer 3 (Atari ST) */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'v') &&
             (in_data[PW_i+3] == '3') )
        {
          if ( testSpecialCruncherData ( 8, 12 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Speed Packer 3 data" , 0 , SpeedPacker3Data );
          }
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
          }
          /* Sound FX */
          if ( testSoundFX13() != BAD )
          {
            Rip_SoundFX13 ();
#ifndef UNIX
            Depack_SoundFX13 ();
#endif
          }
        }
        break;

      case 'T': /* "THX" - AHX */
        if ( ( in_data[PW_i+1] == 'H' ) &&
             ( in_data[PW_i+2] == 'X' ) &&
             (( in_data[PW_i+3] == 0x00 )||( in_data[PW_i+3] == 0x01 )) )
        {
          if ( testTHX() != BAD )
          {
            Rip_THX ();
          }
        }

          /* "TRK1" Module Protector */
        if ( ( in_data[PW_i+1] == 'R' ) &&
             ( in_data[PW_i+2] == 'K' ) &&
             ( in_data[PW_i+3] == '1' ) )
        {
          /* Module Protector */
          if ( testMP_withID() != BAD )
          {
            Rip_MP_withID ();
            Depack_MP ();
          }
        }

	  /* "TMK. Timetracker ?!? */
        if ( ( in_data[PW_i+1] == 'M' ) &&
             ( in_data[PW_i+2] == 'K' ) &&
             ( in_data[PW_i+3] == 0x01 ) )
        {
          if ( testTMK() != BAD )
          {
            Rip_TMK ();
            Depack_TMK ();
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
          if ( testBP() != BAD )
          {
            Rip_BP ();
          }
        }
#ifdef INCLUDEALL
          /* Virtual Dreams VDCO data cruncher */
        if ( (in_data[PW_i+1] == 'D') &&
             (in_data[PW_i+2] == 'C') &&
             (in_data[PW_i+3] == 'O') )
        {
          if ( testSpecialCruncherData ( 12, 8 ) != BAD )
          {
            Rip_SpecialCruncherData ( "Virtual Dreams (VDCO) data cruncher" , 13 , VDCO );
          }
        }
#endif
        break;

      case 'W': /* 0x57 */
          /* "WN" Wanton Packer */
        if ( (in_data[PW_i+1] == 'N') &&
             (in_data[PW_i+2] == 0x00 ) )
        {
          if ( testWN() != BAD )
          {
            Rip_WN ();
            Depack_WN ();
          }
        }
        break;

      case 'X': /* XPKF */
#ifdef INCLUDEALL
          /* xpk'ed file */
        if ( (in_data[PW_i+1] == 'P') &&
             (in_data[PW_i+2] == 'K') &&
             (in_data[PW_i+3] == 'F') )
        {
          if ( testSpecialCruncherData ( 4, 12 ) != BAD )
          {
            Rip_SpecialCruncherData ( "XPK" , 8 , XPK );
          }
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
          }

          /* Promizer 1.0c */
          if ( testPM10c() != BAD )
          {
            Rip_PM10c ();
            Depack_PM10c ();
          }
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
          if ( testPM2() != BAD )
          {
            Rip_PM20 ();
            Depack_PM20 ();
          }
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
          if ( testSpikeCruncher() != BAD )
          {
            Rip_SpikeCruncher ();
          }
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
          if ( testTNMCruncher11() != BAD )
          {
            Rip_TNMCruncher11 ();
          }
        }

          /* "arcD" data cruncher */
        if ( (in_data[PW_i+1]  == 'r') &&
             (in_data[PW_i+2]  == 'c') &&
             (in_data[PW_i+3]  == 'D'))
        {
          if ( testArcDDataCruncher() != BAD )
          {
            Rip_SpecialCruncherData ( "arcD data Cruncher" , 0 , arcD );
          }
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
             (in_data[PW_i+12] == 0xD1) &&
             (in_data[PW_i+13] == 0xFC) )
        {
          if ( testTetrapack_2_1() != BAD )
          {
            Rip_Tetrapack_2_1 ();
          }
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
          if ( testTetrapack_2_2() != BAD )
            Rip_Tetrapack_2_2 ();
        }
        /* High Pressure Cruncher */
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x00) &&
             (in_data[PW_i+3]  == 0xA8) &&
             (in_data[PW_i+4]  == 0x20) &&
             (in_data[PW_i+5]  == 0x7C) &&
             (in_data[PW_i+10] == 0x22) &&
             (in_data[PW_i+11] == 0x7c) &&
             (in_data[PW_i+16] == 0x24) &&
             (in_data[PW_i+17] == 0x48) &&
             (in_data[PW_i+18] == 0x26) &&
             (in_data[PW_i+19] == 0x49) &&
             (in_data[PW_i+20] == 0x61))
        {
          if ( testHighPressureCruncher() != BAD )
          {
            Rip_HighPressureCruncher ();
          }
        }
#endif
        break;

      case 'x': /* xVdg */
#ifdef INCLUDEALL
          /* AMOS sub file */
        if ( (in_data[PW_i+1] == 'V') &&
             (in_data[PW_i+2] == 'd') &&
             (in_data[PW_i+3] == 'g') )
        {
          if ( testSpecialCruncherData ( 8, 4 ) != BAD )
          {
            Rip_SpecialCruncherData ( "xVdg" , 12 , xVdg );
          }
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
          if ( testTetrapack_2_2() != BAD )
            Rip_Tetrapack_2_2 ();
        }

        /* Tetrapack 2.1 case #2*/
        if ( (in_data[PW_i+1]  == 0x00) &&
             (in_data[PW_i+2]  == 0x41) &&
             (in_data[PW_i+3]  == 0xFA) &&
             (in_data[PW_i+4]  == 0x00) &&
             (in_data[PW_i+5]  == 0xE4) &&
             (in_data[PW_i+6]  == 0x4B) &&
             (in_data[PW_i+7]  == 0xF9) &&
             (in_data[PW_i+8]  == 0x00))
        {
          if ( testTetrapack_2_1() != BAD )
          {
            Rip_Tetrapack_2_1 ();
          }
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
          if ( testDefjam32t() != BAD )
          {
            Rip_Defjam32 ();
          }
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
          }
        }
        break;

      case 0xB4:
#ifdef INCLUDEALL
          /* DIET PC data packer */
        if ( (in_data[PW_i+1]  == 0x4C) &&
             (in_data[PW_i+2]  == 0xCD) &&
             (in_data[PW_i+3]  == 0x21) &&
             (in_data[PW_i+4]  == 0x9D) &&
             (in_data[PW_i+5]  == 0x89) &&
             ( ( (in_data[PW_i+6]  == 0x64) &&
             (in_data[PW_i+7]  == 0x6C) &&
             (in_data[PW_i+8]  == 0x7A) ) ||
             ( (in_data[PW_i+6]  == 0x45) &&
             (in_data[PW_i+7]  == 0x4F) &&
             (in_data[PW_i+8]  == 0x53) ) ) )
        {
          if ( testDietDataPacker() != BAD )
          {
            Rip_DietDataPacker ();
          }
        }
#endif
        break;

      case 0xC0:
          /* Pha Packer */
        if ( (PW_i >= 1) && (in_data[PW_i-1] == 0x03) )
        {
          if ( testPHA() != BAD )
          {
            Rip_PHA ();
            Depack_PHA ();
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
  printf ( " 1997-2016 (c) Sylvain \"Asle\" Chipaux (asle@free.fr)\n\n");
/*  getchar();*/
  exit ( 0 );
#endif
  return 0;
}
