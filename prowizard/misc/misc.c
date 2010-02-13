#include "globals.h"
#include "extern.h"

/*
 *  at now, when this fonction is called, no global var has been used ...
 * and can be here :). (save for the input file pointer ...)
*/
#if 0
void Support_Types ( void )
{
  long types_file_size, cpt;
  char read_line[_TYPES_LINE_LENGHT];
  FILE *types_file;

  /* fill with $00 ... "Extension" is global */
  memset ( Extensions, 0, sizeof Extensions );

  types_file = fopen ( _TYPES_FILENAME , "rb" );
  if ( types_file == NULL )
  {
    printf ( "!!! couldn't find \"%s\" file !. Default extension used.\n"
             , _TYPES_FILENAME );
    Support_Types_FileDefault ();
    return;
  }

  /* get "_TYPES_" size */
  types_file_size = PWGetFileSize ( _TYPES_FILENAME );
  fseek ( types_file , 0 , 0 ); /* just to be sure. put the fp back at the beginning */

  PW_i = 0;  /* will inc up to _KNOWN_FORMATS */
  while ( ftell ( types_file )+1 < types_file_size )
  {
    memset ( read_line, 0, _TYPES_LINE_LENGHT );
    fgets ( read_line , _TYPES_LINE_LENGHT , types_file );
    if ( read_line[0] == '#' )
      continue;
    if ( sizeof ( read_line ) < 2 )
    {
      printf ( "!!! Damaged \"%s\" file at non-commented line %ld\n"
             , _TYPES_FILENAME , PW_i+1 );
      PW_i = 99999l;
      break;
    }
    cpt = 0;
    while ( read_line[cpt] != 0x00 && read_line[cpt] != 0x0a && read_line[cpt] != 0x0d )
    {
      Extensions[PW_i][cpt] = read_line[cpt];
      cpt += 1;
    }
    /*printf ( "[%ld]%ld:%s," , PW_i,ftell (types_file),read_line );*/
    PW_i += 1;
    if ( PW_i == _KNOWN_FORMATS )
      break;
  }

  if ( PW_i != _KNOWN_FORMATS )
  {
    printf ( "!!! Damaged \"%s\" file. Missing up %ld extensions definitions\n"
             , _TYPES_FILENAME , _KNOWN_FORMATS-(PW_i+1));
    Support_Types_FileDefault ();
    return;
  }

  fclose ( types_file );
}
#endif

/*Uchar *XighExtensions[_KNOWN_FORMATS+1];*/

/*
 * fill the global "Extension" with default extensions if a pb happened
 * while reading "_TYPES_" editable extensions file
*/
void Support_Types_FileDefault ( void )
{
  /* xigh examples */
  /*  strdup( Extensions[0], "AC1" );
      strdup( Extensions[12], "MegaBixExtension" );*/

  /* note: "_TYPES_" file first entry is 1 ! */
  strcpy ( Extensions[0]  , "ac1d" );
  strcpy ( Extensions[1]  , "bp" );
  strcpy ( Extensions[2]  , "fc-m" );
  strcpy ( Extensions[3]  , "hrt" );
  strcpy ( Extensions[4]  , "kris" );
  strcpy ( Extensions[5]  , "PowerMusic" );
  strcpy ( Extensions[6]  , "Promizer10c" );
  strcpy ( Extensions[7]  , "Promizer18a" );
  strcpy ( Extensions[8]  , "Promizer20" );
  strcpy ( Extensions[9]  , "ProRunner1" );
  strcpy ( Extensions[10] , "ProRunner2" );
  strcpy ( Extensions[11] , "skyt" );
  strcpy ( Extensions[12] , "WantonPacker" );
  strcpy ( Extensions[13] , "xann" );
  strcpy ( Extensions[14] , "ModuleProtector" );
  strcpy ( Extensions[15] , "DigitalIllusion" );
  strcpy ( Extensions[16] , "PhaPacker" );
  strcpy ( Extensions[17] , "Promizer01" );
  strcpy ( Extensions[18] , "ProPacker21" );
  strcpy ( Extensions[19] , "ProPacker30" );
  strcpy ( Extensions[20] , "Eureka" );
  strcpy ( Extensions[21] , "StarTrekkerPack" );
  strcpy ( Extensions[22] , "mod" );
  strcpy ( Extensions[23] , "unic1" );
  strcpy ( Extensions[24] , "unic2" );
  strcpy ( Extensions[25] , "Fuzzac" );
  strcpy ( Extensions[26] , "gmc" );
  strcpy ( Extensions[27] , "crb" );
  strcpy ( Extensions[28] , "ksm" );
  strcpy ( Extensions[29] , "Noiserunner" );
  strcpy ( Extensions[30] , "NoisePacker1" );
  strcpy ( Extensions[31] , "NoisePacker2" );
  strcpy ( Extensions[32] , "NoisePacker3" );
  strcpy ( Extensions[33] , "P40A" );
  strcpy ( Extensions[34] , "P40B" );
  strcpy ( Extensions[35] , "P41A" );
  strcpy ( Extensions[36] , "Promizer4" );
  strcpy ( Extensions[37] , "ProPacker1" );
  strcpy ( Extensions[38] , "TrackerPacker1" );
  strcpy ( Extensions[39] , "TrackerPacker2" );
  strcpy ( Extensions[40] , "TrackerPacker3" );
  strcpy ( Extensions[41] , "ZenPacker" );
  strcpy ( Extensions[42] , "P50A" );
  strcpy ( Extensions[43] , "P60A" );
  strcpy ( Extensions[44] , "mod" );
  strcpy ( Extensions[45] , "StoneCrackerData" );
  strcpy ( Extensions[46] , "StoneCracker270 " );
  strcpy ( Extensions[47] , "P61A" );
  strcpy ( Extensions[48] , "stim" );
  strcpy ( Extensions[49] , "mod" );
  strcpy ( Extensions[50] , "TetraPack22" );
  strcpy ( Extensions[51] , "CrunchmaniaData" );
  strcpy ( Extensions[52] , "DefjamCruncher" );
  strcpy ( Extensions[53] , "Tetrapack21" );
  strcpy ( Extensions[54] , "ice" );
  strcpy ( Extensions[55] , "ByteKiller" );
  strcpy ( Extensions[56] , "xpk" );
  strcpy ( Extensions[57] , "Imploder" );
  strcpy ( Extensions[58] , "rnc" );
  strcpy ( Extensions[59] , "DoubleAction" );
  strcpy ( Extensions[60] , "PowerPacker3" );
  strcpy ( Extensions[61] , "PowerPacker4" );
  strcpy ( Extensions[62] , "PowerPacker23" );
  strcpy ( Extensions[63] , "SpikeCruncher" );
  strcpy ( Extensions[64] , "Tetrapack102" );
  strcpy ( Extensions[65] , "TimeCruncher17" );
  strcpy ( Extensions[66] , "MasterCruncher" );
  strcpy ( Extensions[67] , "MegaCruncher" );
  strcpy ( Extensions[68] , "jam" );
  strcpy ( Extensions[69] , "BSI-FC" );
  strcpy ( Extensions[70] , "digi" );
  strcpy ( Extensions[71] , "qc" );
  strcpy ( Extensions[72] , "TheDarkDemon" );
  strcpy ( Extensions[73] , "FuchsTracker" );
  strcpy ( Extensions[74] , "SynchroPacker46" );
  strcpy ( Extensions[75] , "TNMCruncher11" );
  strcpy ( Extensions[76] , "SuperCruncher27" );
  strcpy ( Extensions[77] , "PPbk" );
  strcpy ( Extensions[78] , "RelokIt1" );
  strcpy ( Extensions[79] , "StoneCracker292data" );
  strcpy ( Extensions[80] , "fire" );
  strcpy ( Extensions[81] , "MacPacker12" );
  strcpy ( Extensions[82] , "SoundFX13" );
  strcpy ( Extensions[83] , "arcD" );
  strcpy ( Extensions[84] , "para" );
  strcpy ( Extensions[85] , "crnd" );
  strcpy ( Extensions[86] , "-sb-" );
  strcpy ( Extensions[87] , "sf" );
  strcpy ( Extensions[88] , "RLE" );
  strcpy ( Extensions[89] , "VDC0" );
  strcpy ( Extensions[90] , "sq" );
  strcpy ( Extensions[91] , "sp" );
  strcpy ( Extensions[92] , "ST26" );
  strcpy ( Extensions[93] , "IT10" );
  strcpy ( Extensions[94] , "HQCCruncher2" );
  strcpy ( Extensions[95] , "TtyItCruncher101" );
  strcpy ( Extensions[96] , "FC13" );
  strcpy ( Extensions[97] , "FC14" );
  strcpy ( Extensions[98] , "1AM" );
  strcpy ( Extensions[99] , "2AM" );
  strcpy ( Extensions[100], "med" );
  strcpy ( Extensions[101], "AceCruncherData" );
  strcpy ( Extensions[102], "Newtron" );
  strcpy ( Extensions[103], "GPMO" );
  strcpy ( Extensions[104], "PolkaPacker" );
  strcpy ( Extensions[105], "GnuPlayer" );
  strcpy ( Extensions[106], "CJ_DataCruncher" );
  strcpy ( Extensions[107], "AmBk" );
  strcpy ( Extensions[108], "MasterCruncher3data" );
  strcpy ( Extensions[109], "xm" );
  strcpy ( Extensions[110], "MegaCruncherObj" );
  strcpy ( Extensions[111], "TurboSqueezer61" );
  strcpy ( Extensions[112], "StoneCracker299d" );
  strcpy ( Extensions[113], "StoneCracker310" );
  strcpy ( Extensions[114], "StoneCracker299b" );
  strcpy ( Extensions[115], "StoneCracker299" );
  strcpy ( Extensions[116], "StoneCracker300" );
  strcpy ( Extensions[117], "ThePlayer30a" );
  strcpy ( Extensions[118], "ThePlayer22a" );
  strcpy ( Extensions[119], "NoiseFromHeaven" );
  strcpy ( Extensions[120], "TMK" );
  strcpy ( Extensions[121], "DragPack252" );
  strcpy ( Extensions[122], "DragPack100" );
  strcpy ( Extensions[123], "SPv3" );
  strcpy ( Extensions[124], "AtomikPackerData" );
  strcpy ( Extensions[125], "AutomationPackerData" );
  //  strcpy ( Extensions[125], "TreasurePattern" );
  strcpy ( Extensions[126], "SGTPacker" );
  strcpy ( Extensions[127], "GNUPacker12" );
  strcpy ( Extensions[128], "CrunchmaniaSimple" );
  strcpy ( Extensions[129], "dmu" );
  strcpy ( Extensions[130], "TitanicsPlayer" );
  strcpy ( Extensions[131], "NewtronOld" );
  strcpy ( Extensions[132], "NovoTrade" );
  strcpy ( Extensions[133], "Skizzo" );
  strcpy ( Extensions[134], "StoneArtsPlayer" );
  strcpy ( Extensions[135], "---" );
}




/*
 * saving what's found. Mainly music file here.
 * PW_Start_Address & OutputSize are global .. not everybody likes
 * that :(. I just cant seem to manage it otherwise.
*/
void Save_Rip ( char * format_to_save, int FMT_EXT )
{
  Save_Status = BAD;
  pw_write_log ( "%s found at %ld !. its size is : %ld\n", format_to_save , PW_Start_Address , OutputSize );
  if ( (PW_Start_Address + (long)OutputSize) > PW_in_size )
  {
    pw_write_log ( "!!! Truncated, missing (%ld byte(s) !)\n"
             , (PW_Start_Address+OutputSize)-PW_in_size );
    PW_i += 2 ;
    return;
  }
  BZERO ( OutName_final, sizeof OutName_final);
  sprintf ( OutName_final , "%ld.%s" , Cpt_Filename , Extensions[FMT_EXT] );
  pw_write_log ( "  saving in file \"%s\" ... " , OutName_final );
  Cpt_Filename += 1;
  PW_out = moduleripper2_fopen ( OutName_final , "w+b", format_to_save, PW_Start_Address, OutputSize);
  //PW_out = PW_fopen ( OutName_final , "w+b" );
  if (!PW_out)
      return;
  fwrite ( &in_data[PW_Start_Address] , OutputSize , 1 , PW_out );
  fclose ( PW_out );
  pw_write_log ( "done\n" );
  if ( CONVERT == GOOD )
  {
    pw_write_log ( "  converting to Protracker ... " );
  }
  //fflush ( stdout );
  Save_Status = GOOD;
}

/*
 * Special cases for files with header to rebuild ...
 *
*/
void Save_Rip_Special ( char * format_to_save, int FMT_EXT, Uchar * Header_Block , Ulong Block_Size )
{
  Save_Status = BAD;
  pw_write_log ( "%s found at %ld !. its size is : %ld\n", format_to_save , PW_Start_Address , OutputSize );
  if ( (PW_Start_Address + (long)OutputSize) > PW_in_size )
  {
    pw_write_log ( "!!! Truncated, missing (%ld byte(s) !)\n"
             , (PW_Start_Address+OutputSize)-PW_in_size );
    PW_i += 2 ;
    return;
  }
  BZERO (OutName_final, sizeof OutName_final);
  sprintf ( OutName_final , "%ld.%s" , Cpt_Filename , Extensions[FMT_EXT] );
  pw_write_log ( "  saving in file \"%s\" ... " , OutName_final );
  Cpt_Filename += 1;
//  PW_out = PW_fopen ( OutName_final , "w+b" );
  PW_out = moduleripper2_fopen ( OutName_final , "w+b", format_to_save, PW_Start_Address, OutputSize );
  fwrite ( Header_Block , Block_Size  , 1 , PW_out );
  fwrite ( &in_data[PW_Start_Address] , OutputSize , 1 , PW_out );
  fclose ( PW_out );
  pw_write_log ( "done\n" );
  if ( CONVERT == GOOD )
  {
    pw_write_log ( "  converting to Protracker ... " );
  }
  pw_write_log ( "  Header of this file was missing and has been rebuilt !\n" );
  if ( FMT_EXT == DragPack252)
    pw_write_log ( "  WARNING !: it's a fake header since in this case !!\n" );
  //fflush ( stdout );
  Amiga_EXE_Header = GOOD;
  Save_Status = GOOD;
}



/* writing craps in converted MODs */
void Crap ( char *Format , Uchar Delta , Uchar Pack , FILE *out )
{
  fseek ( out , 560 , SEEK_SET );
  fprintf ( out , "[  Converted with  ]" );
  fseek ( out , 590 , SEEK_SET );
  fprintf ( out , "[ ProWizard for PC ]" );
  fseek ( out , 620 , SEEK_SET );
  fprintf ( out , "[ written by Asle! ]" );

  fseek ( out , 680 , SEEK_SET );
  fprintf ( out , "[ Original Format: ]" );
  fseek ( out , 710 , SEEK_SET );
  fprintf ( out , "[%s]" , Format );

  if ( Delta == GOOD )
  {
    fseek ( out , 770 , SEEK_SET );
    fprintf ( out , "[! smp were DELTA  ]" );
  }
  if ( Pack == GOOD )
  {
    fseek ( out , 800 , SEEK_SET );
    fprintf ( out , "[! smp were PACKED ]" );
  }
}

/* writing craps in converted MODs */
void Crap15 ( char *Format , Uchar Delta , Uchar Pack , FILE *out )
{
  fseek ( out , 260 , SEEK_SET );
  fprintf ( out , "[  Converted with  ]" );
  fseek ( out , 290 , SEEK_SET );
  fprintf ( out , "[ ProWizard for PC ]" );
  fseek ( out , 320 , SEEK_SET );
  fprintf ( out , "[ written by Asle! ]" );

  fseek ( out , 380 , SEEK_SET );
  fprintf ( out , "[ Original Format: ]" );
  fseek ( out , 410 , SEEK_SET );
  fprintf ( out , "[%s]" , Format );
}

/*
 * Special version of Test() for cruncher data (Ice! etc...)
 * only one file and not hundreds ...
*/
short testSpecialCruncherData ( long Pack_addy , long Unpack_addy )
{
  PW_Start_Address = PW_i;

  /* a small test preventing hangover :) ... */
  /* e.g. addressing of unassigned data */
  if ( ( (long)PW_i + Pack_addy ) > PW_in_size )
  {
/*printf ( "#0\n" );*/
    return BAD;
  }

  /* packed size */
  /* first byte is sometime used ... "SQ is an ex" */
  PW_l = ( (in_data[PW_Start_Address+Pack_addy+1]*256*256) +
           (in_data[PW_Start_Address+Pack_addy+2]*256) +
            in_data[PW_Start_Address+Pack_addy+3] );
  /* unpacked size */
  PW_k = ( (in_data[PW_Start_Address+Unpack_addy]*256*256*256) +
           (in_data[PW_Start_Address+Unpack_addy+1]*256*256) +
           (in_data[PW_Start_Address+Unpack_addy+2]*256) +
            in_data[PW_Start_Address+Unpack_addy+3] );

  if ( (PW_k <= 2) || (PW_l <= 2) )
  {
/*printf ( "#1\n" );*/
    return BAD;
  }

  if ( PW_l > 0x989680 ) /* 10 mb */
  {
/*printf ( "#2\n" );*/
    return BAD;
  }

  if ( PW_k <= PW_l )
  {
/*printf ( "#3\n" );*/
    return BAD;
  }

  if ( PW_k > 0x989689 )  /* 10 Megs ! */
  {
/*printf ( "#4\n" );*/
    return BAD;
  }

  return GOOD;
}



/*
 * Special version of Rip() for cruncher data (Ice! etc...)
 * only one file and not hundreds ...
*/
void Rip_SpecialCruncherData ( char *Packer_Name , int Header_Size , int Packer_Extension_Define )
{
  /* PW_l IS the whole size -Header_Size */
  /* various Data crunchers need a little calculation beside the "+" or "-" */
  switch (Header_Size)
  {
    case 999991: /* SQ data cruncher */
      PW_l *= 4;
      PW_l += 10;
      OutputSize = PW_l;
      break;
    default:
      OutputSize = PW_l + Header_Size;
  }

  /* printf ( "\b\b\b\b\b\b\b\b%s file found at %ld !. its size is : %ld\n" , Packer_Name , PW_Start_Address , OutputSize );*/
  /*  OutName[1] = Extensions[Packer_Extension_Define][0];
  OutName[2] = Extensions[Packer_Extension_Define][1];
  OutName[3] = Extensions[Packer_Extension_Define][2];*/

  CONVERT = BAD;
  Save_Rip ( Packer_Name, Packer_Extension_Define );
  
  if ( Save_Status == GOOD )
//    PW_i += (OutputSize - 2);  /* 0 should do but call it "just to be sure" :) */
    PW_i += (Header_Size + 1);  /* test to overcome fake datas */
  PW_WholeSampleSize = 0;

}


/* yet again on Xigh's suggestion. How to handle 'correctly' a file size */
long PWGetFileSize (char * infile)
{
  long i;
  struct stat *Stat;
  Stat = (struct stat *) malloc ( sizeof (struct stat));
  stat ( infile, Stat );
  i = (long)Stat->st_size;
  free ( Stat );
  return i;
}

#if 0
/* Same as fopen() but saves a lot of tests, done only here. */
/* Done to check if the output file could be created */
FILE * PW_fopen (char *filename, char fopenargs[3] )
{
  FILE *local_out;
  local_out = fopen (filename, fopenargs);
  if (local_out == NULL)
  {
    printf ("!!couldn't create the file \"%s\"!\nexiting...",filename);
    exit (-1);
  }
  return local_out;
}
#endif 
FILE * PW_fopen (char *filename, char *fopenargs)
{
    return moduleripper_fopen (filename, fopenargs);
}

/* fills a var with all the pitch for PTK */
/* doing a function instead of a lot of includes ...*/
void fillPTKtable (Uchar poss[37][2])
{
  poss[0][0]=0x00,  poss[0][1]=0x00;

  poss[1][0]=0x03,  poss[1][1]=0x58;
  poss[2][0]=0x03,  poss[2][1]=0x28;
  poss[3][0]=0x02,  poss[3][1]=0xfa;
  poss[4][0]=0x02,  poss[4][1]=0xd0;
  poss[5][0]=0x02,  poss[5][1]=0xa6;
  poss[6][0]=0x02,  poss[6][1]=0x80;   /*  1  */
  poss[7][0]=0x02,  poss[7][1]=0x5c;
  poss[8][0]=0x02,  poss[8][1]=0x3a;
  poss[9][0]=0x02,  poss[9][1]=0x1a;
  poss[10][0]=0x01,  poss[10][1]=0xfc;
  poss[11][0]=0x01,  poss[11][1]=0xe0;
  poss[12][0]=0x01,  poss[12][1]=0xc5;

  poss[13][0]=0x01,  poss[13][1]=0xac;
  poss[14][0]=0x01,  poss[14][1]=0x94;
  poss[15][0]=0x01,  poss[15][1]=0x7d;
  poss[16][0]=0x01,  poss[16][1]=0x68;
  poss[17][0]=0x01,  poss[17][1]=0x53;
  poss[18][0]=0x01,  poss[18][1]=0x40;   /*  2  */
  poss[19][0]=0x01,  poss[19][1]=0x2e;
  poss[20][0]=0x01,  poss[20][1]=0x1d;
  poss[21][0]=0x01,  poss[21][1]=0x0d;
  poss[22][0]=0x00,  poss[22][1]=0xfe;
  poss[23][0]=0x00,  poss[23][1]=0xf0;
  poss[24][0]=0x00,  poss[24][1]=0xe2;

  poss[25][0]=0x00,  poss[25][1]=0xd6;
  poss[26][0]=0x00,  poss[26][1]=0xca;
  poss[27][0]=0x00,  poss[27][1]=0xbe;
  poss[28][0]=0x00,  poss[28][1]=0xb4;
  poss[29][0]=0x00,  poss[29][1]=0xaa;
  poss[30][0]=0x00,  poss[30][1]=0xa0;   /*  3  */
  poss[31][0]=0x00,  poss[31][1]=0x97;
  poss[32][0]=0x00,  poss[32][1]=0x8f;
  poss[33][0]=0x00,  poss[33][1]=0x87;
  poss[34][0]=0x00,  poss[34][1]=0x7f;
  poss[35][0]=0x00,  poss[35][1]=0x78;
  poss[36][0]=0x00,  poss[36][1]=0x71;
  return;
}
