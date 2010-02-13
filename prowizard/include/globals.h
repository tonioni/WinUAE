/*
 * ProWizard PC include file
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
/*#include <gtk/gtk.h>*/
#ifdef DOS
#include <winsock2.h>
#endif
#ifdef DMALLOC
#include "dmalloc.h"
#endif


typedef unsigned char Uchar;
typedef unsigned long Ulong;
typedef unsigned short Ushort;

#ifdef _WIN32
__inline Ulong htonlx (Ulong v)
{
    return (v >> 24) | ((v >> 8) & 0xff00) | (v << 24) | ((v << 8) & 0xff0000);
}
#endif

#define _KNOWN_FORMATS      135
#define _TYPES_FILENAME     "_types_"
#define _TYPES_LINE_LENGHT  256
#define MINIMAL_FILE_LENGHT 64
#define GOOD                0
#define BAD                 1
#define BZERO(a,b)          memset(a,0,b)

enum
{
  AC1D_packer = 0,
/* version 2 / 3 */
  SoundMonitor,
  FC_M_packer,
  Hornet_packer,
  KRIS_tracker,
  Power_Music,
  Promizer_10c,
  Promizer_18a,
  Promizer_20,
  ProRunner_v1,
  ProRunner_v2,
  SKYT_packer,
  Wanton_packer,
  XANN_packer,
  Module_protector,
  Digital_illusion,
  Pha_packer,
  Promizer_01,
  Propacker_21,
  Propacker_30,
  Eureka_packer,
  Star_pack,
  Protracker,
  UNIC_v1,
  UNIC_v2,
  Fuzzac,
  GMC,
  Heatseeker,
  KSM,
  Noiserunner,
  Noisepacker1,
  Noisepacker2,
  Noisepacker3,
  P40A,
  P40B,
  P41A,
  PM40,
  PP10,
  TP1,
  TP2,
  TP3,
  ZEN,
  P50A,
  P60A,
  StarTrekker,
  /* stands for S404(data/exe),S401(data),S403(data) and S310,S300(data) */
  S404,
  StoneCracker270,
  P61A,
  STIM,
  SoundTracker,
  TPACK22,
/* stands for CrM! & CrM2 and Crunchmania Address*/
  CRM1,
/* stands for both Defjam 3.2 & 3.2 pro */
  Defjam_32,
  TPACK21,
  ICE,
/* stands for version 1.3 , 2.0 , 3.0 and Pro 1.0*/
  ByteKiller,
  XPK,
  IMP,
  RNC,
  Double_Action,
  Powerpacker3,
  Powerpacker4,
  Powerpacker23,
  SpikeCruncher,
  TPACK102,
  TimeCruncher,
  MasterCruncher,
/* stands also for Mega Cruncher 1.0/1.2 */
  MegaCruncher,
  JamCracker,
  BSIFC,
  DigiBooster,
  QuadraComposer,
  TDD,
  FuchsTracker,
  SyncroPacker,
  TNMCruncher,
  SuperCruncher,
/* not for PP20 themselves :) ... only PP20 subfiles inside PPbk */
  PP20,
  RelokIt,
  STC292data,
  FIRE,
  MaxPacker,
  SoundFX,
  arcD,
  PARA,
  CRND,
  SB_DataCruncher,
  SF,
  RLE,
  VDCO,
  SQ,
  SP,
  STK26,
  IceTracker,
  HQC,
  TryIt,
  FC13,
  FC14,
  AmnestyDesign1,
  AmnestyDesign2,
  MED,
  ACECruncherData,
  Newtron,
  GPMO,
  PolkaPacker,
  GnuPlayer,
  CJ_DataCruncher,
  AmBk,
  MasterCruncher3data,
  XM,
  MegaCruncherObj,
  TurboSqueezer61,
  STC299d,
  STC310,
  STC299b,
  STC299,
  STC300,
  ThePlayer30a,
  ThePlayer22a,
  NoiseFromHeaven,
  TMK,
  DragPack252,
  DragPack100,
  SpeedPacker3Data,
  AtomikPackerData,
  AutomationPackerData,
  /*  TreasurePattern,*/
  SGTPacker,
  GNUPacker12,
  CrunchmaniaSimple,
  dmu,
  TitanicsPlayer,
  NewtronOld,
  NovoTrade,
  Skizzo,
  StoneArtsPlayer,
};
