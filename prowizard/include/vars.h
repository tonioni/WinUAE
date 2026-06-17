/*
 * "vars.h"
 *
 * Part of Pro-Wizard-1 Package
 * (c) Sylvain "Asle" Chipaux
*/


FILE      *PW_in,*PW_out;
int32_t   PW_Start_Address=0;
uint32_t  OutputSize=0;
int32_t   PW_in_size;
int32_t   Cpt_Filename=0l;
uint32_t  PW_i;
uint32_t  PW_j,PW_k,PW_l,PW_m,PW_n,PW_o;
uint8_t   *in_data;
/*uint8_t OutName[5]={'.','-','-','-',0x00};*/
char      OutName_final[33];
char      Depacked_OutName[33];
uint8_t   Save_Status = GOOD;
uint32_t  PW_WholeSampleSize=0;
char      Extensions[_KNOWN_FORMATS+1][33];
uint8_t   CONVERT = BAD;
uint8_t   Amiga_EXE_Header = GOOD;
