/*
 * "vars.h"
 *
 * Part of Pro-Wizard-1 Package
 * (c) Sylvain "Asle" Chipaux
*/


FILE  *PW_in,*PW_out;
long  PW_Start_Address=0;
Ulong OutputSize=0;
long  PW_in_size;
long  Cpt_Filename=0l;
Ulong PW_i;
Ulong PW_j,PW_k,PW_l,PW_m,PW_n,PW_o;
Uchar *in_data;
/*Uchar OutName[5]={'.','-','-','-',0x00};*/
char OutName_final[33];
char Depacked_OutName[33];
Uchar Save_Status = GOOD;
Ulong PW_WholeSampleSize=0;
char Extensions[_KNOWN_FORMATS+1][33];
Uchar CONVERT = BAD;
Uchar Amiga_EXE_Header = GOOD;
