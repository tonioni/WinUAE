/* testPMD3() */
/* Rip_PMD3() */
/* Depack_PMD3() */


#include "globals.h"
#include "extern.h"


int16_t	 testPMD3 ( void )
{
  /* test 1 */
  if ( PW_i < 1080 )
  {
    /*printf ( "#1 (PW_i:%d)\n" , PW_i );*/
    return BAD;
  }
  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test 2 */
  PW_Start_Address = PW_i-1080;
  
  /* test 1.1 */
  if ( PW_Start_Address + 12 > PW_in_size )
  {
    /*printf ( "#1.1 (PW_i:%d)\n" , PW_i );*/
    return BAD;
  }

  
  for ( PW_k=0 ; PW_k<31 ; PW_k++ )
  {
    /* size */
    PW_j = (((in_data[PW_Start_Address+42+PW_k*30]*256)+in_data[PW_Start_Address+43+PW_k*30])*2);
    /* loop start */
    PW_m = (((in_data[PW_Start_Address+46+PW_k*30]*256)+in_data[PW_Start_Address+47+PW_k*30])*2);
    /* loop size */
    PW_n = (((in_data[PW_Start_Address+48+PW_k*30]*256)+in_data[PW_Start_Address+49+PW_k*30])*2);

    if ( test_smps (PW_j,PW_m,PW_n,in_data[PW_Start_Address+45+PW_k*30]/2,in_data[PW_Start_Address+44+PW_k*30]) == BAD )
    {
      /*printf ( "#2 (Start:%d)(siz:%d)(lstart:%d)(lsiz:%d) (smp:%d)\n" , PW_Start_Address,PW_j,PW_m,PW_n,PW_k );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #4  pattern list size */
  PW_l = in_data[PW_Start_Address+950];
  if ( PW_l>127 )
  {
    /*printf ( "#4,0 (Start:%d)\n" , PW_Start_Address );*/
    return BAD;
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_l holds the size of the pattern list */
  PW_k=0;
  for ( PW_j=0 ; PW_j<128 ; PW_j++ )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > PW_k )
      PW_k = in_data[PW_Start_Address+952+PW_j];
    if ( in_data[PW_Start_Address+952+PW_j] > 127 )
    {
      /*printf ( "#4,1 (Start:%d)\n" , PW_Start_Address );*/
      return BAD;
    }
  }

  /*if ( PW_Start_Address == 0)printf ("yo");*/

  /* PW_k holds the highest pattern number */
  /* test last patterns of the pattern list = 0 ? */
  PW_j += 2; /* found some obscure ptk :( */
  while ( PW_j < 128 )
  {
    if ( in_data[PW_Start_Address+952+PW_j] > 0x7f )
    {
      /*printf ( "#4,2 (Start:%d) (PW_j:%d) (at:%d)\n" , PW_Start_Address,PW_j ,PW_Start_Address+952+PW_j );*/
      return BAD;
    }
    PW_j += 1;
  }
  /* PW_k is the number of pattern in the file (-1) */
  PW_k += 1;

  /*  if ( PW_Start_Address == 0)printf ("yo");*/

  /* test #5 size ... */
  PW_m = ((in_data[PW_Start_Address+1084]*256*256*256)+
          (in_data[PW_Start_Address+1085]*256*256)+
          (in_data[PW_Start_Address+1086]*256)+
          in_data[PW_Start_Address+1087]);
  PW_o = ((in_data[PW_Start_Address+1088]*256*256*256)+
          (in_data[PW_Start_Address+1089]*256*256)+
          (in_data[PW_Start_Address+1090]*256)+
          in_data[PW_Start_Address+1091]);
  if ( (PW_o + PW_n +PW_Start_Address) > PW_in_size )
  {
    /*printf ( "#5,0 (Start:%d)\n" , PW_Start_Address );*/
    return BAD;
  }

  return GOOD;
}


void Rip_PMD3 ( void )
{
  uint8_t c=0x00;
  /* PW_k is still the nbr of pattern */
  /* PW_m is still the size of note pointers */
  /* PW_o is still the size of ref table for notes */
  
  if (in_data[PW_Start_Address + 1082] == 'D')
    c = 0x20;
  else /* 'd' */
    c = 0x40;

  PW_WholeSampleSize = 0;
  for ( PW_j=0 ; PW_j<31 ; PW_j++ )
    PW_WholeSampleSize += (((in_data[PW_Start_Address+42+PW_j*30]*256)+in_data[PW_Start_Address+43+PW_j*30])*2);

  OutputSize += PW_WholeSampleSize + PW_m + PW_o + 1092 + (PW_k*c);

  CONVERT = BAD;
  if (c == 0x20)
    Save_Rip ( "Module-Patterncompressor (PMD3)", PMD3 );
  else
    Save_Rip ( "Module-Patterncompressor (PMd3)", PMd3 );
  
  if ( Save_Status == GOOD )
    PW_i += 1;
}



/*
 *   PMD3.c   20160413 (c) Asle
 *
 * Converts PMD3 MODs back to PTK or OCT
 *
*/

void Depack_PMD3 ( void )
{
  uint8_t c=0x00;
  uint8_t *Whatever;
  uint8_t Max=0x00;
  int32_t	 WholeSampleSize=0;
  int32_t	 i=0, addy1, addy2, addy3, size1, size2, size3, ptr1, ptr2, ptr3;
  int32_t	 Where=PW_Start_Address;
  FILE *out; /*, *DEBUG;*/

  if ( Save_Status == BAD )
    return;

  sprintf ( Depacked_OutName , "%d.mod" , Cpt_Filename-1 );
  out = PW_fopen ( Depacked_OutName , "w+b" );

/*  DEBUG = fopen ("_debug_.txt", "w+b");*/

  /* get whole sample size and patch vols (/2)*/
  for ( i=0 ; i<31 ; i++ )
  {
    WholeSampleSize += (((in_data[Where+42+i*30]*256)+in_data[Where+43+i*30])*2);
  }
  /*printf ( "Whole sanple size : %ld\n" , WholeSampleSize );*/

  /* read and write whole header */
  fwrite ( &in_data[Where] , 1080 , 1 , out );

  /* write ID */
  if (in_data[PW_Start_Address + 1082] == 'D')
    c = 0x20;
  else /* 'd' */
    c = 0x40;
  
  Whatever = (uint8_t *) malloc (4);
  if (c == 0x20)
  {
    Whatever[0] = 'M';
    Whatever[1] = '.';
    Whatever[2] = 'K';
    Whatever[3] = '.';
  }
  else
  {
    Whatever[0] = 'C';
    Whatever[1] = 'D';
    Whatever[2] = '8';
    Whatever[3] = '1';
  }
  fwrite ( Whatever , 4 , 1 , out );
  free ( Whatever );

  Where += 952;

  /* get number of pattern */
  Max = 0x00;
  for ( i=0 ; i<128 ; i++ )
  {
    if ( in_data[Where+i] > Max )
      Max = in_data[Where+i];
  }
  Max += 1;
  /*printf ( "Number of pattern : %d\n" , Max );*/

  Where += (128 + 4);

  ptr1 = addy1 = Where + 8;
  size1 = Max * c; /* c says if it's 4 or 8 channels */
  ptr2 = addy2 = addy1 + size1;
  size2 = ((in_data[Where+0]*256*256*256)+
           (in_data[Where+1]*256*256)+
           (in_data[Where+2]*256)+
           in_data[Where+3]);
  ptr3 = addy3 = addy2 + size2;
  size3 = ((in_data[Where+4]*256*256*256)+
           (in_data[Where+5]*256*256)+
           (in_data[Where+6]*256)+
           in_data[Where+7]);
  

  /* pattern data */
  for (i=0; i<Max; i++) /* loop per pattern */
  {
    uint8_t PATTERN[2048], j, k, l;
    BZERO (PATTERN, 2048);

/*fprintf (DEBUG,"Pattern %d\n-------\n",i);*/
    
    /* loop per voice */
    for (j=0; j<(c/8); j++)
    {
/*fprintf (DEBUG,"voice %d\n-----\n",j);*/
      PW_j = ((in_data[ptr1+0]*256*256*256)+
              (in_data[ptr1+1]*256*256)+
              (in_data[ptr1+2]*256)+
              in_data[ptr1+3]);
      ptr1 += 4;
      PW_k = ((in_data[ptr1+0]*256*256*256)+
              (in_data[ptr1+1]*256*256)+
              (in_data[ptr1+2]*256)+
              in_data[ptr1+3]);
      ptr1 += 4;
/*      fprintf (DEBUG,"ctrl:%4x data:%4x\n", PW_j, PW_k);*/
      
      for (k=0, l=0; k<64;) /* k will count the note occurences */
      {
        uint32_t z, repeat;
        uint8_t c1 = (in_data[PW_j+ptr2+l]&0x03)+1, c2 = (in_data[PW_j+ptr2+l]&0xFC);
/*        fprintf (DEBUG, "[@%x][@%x] %x -> %d & %d => ",PW_j+ptr2+l, PW_j+l, in_data[PW_j+ptr2+l], c1,c2);*/
        l+=1;
        
        /* fetch relative note and write it */
/*        fprintf (DEBUG,"%x-%x-%x-%x | ",
        in_data[PW_k+ptr3+c2],
        in_data[PW_k+ptr3+c2+1],
        in_data[PW_k+ptr3+c2+2],
        in_data[PW_k+ptr3+c2+3]
        );*/
        for (repeat=0; repeat<c1; repeat+=1)
        {
          z = (j*4)+((k+repeat)*(c/2));
        
/*          fprintf (DEBUG,"(z:%x)\n",z);
          fflush (DEBUG);*/
        
          PATTERN[z] = in_data[PW_k+ptr3+c2];
          PATTERN[z+1] = in_data[PW_k+ptr3+c2+1];
          PATTERN[z+2] = in_data[PW_k+ptr3+c2+2];
          PATTERN[z+3] = in_data[PW_k+ptr3+c2+3];
        }
        
/*        fprintf (DEBUG,"%x-%x-%x-%x | (z:%x)(z+1:%x)(z+2:%x)(z+3:%x)",PATTERN[z],PATTERN[z+1],PATTERN[z+2],PATTERN[z+3], z, z+1, z+2, z+3);
        fprintf (DEBUG,"\n");*/
        
        k += c1;
      }
/*      fprintf (DEBUG,"\n");*/
    }
    
    
    
    /* write pattern */
    if (c==0x20)
      fwrite ( PATTERN , 1024 , 1 , out );
    else
      fwrite ( PATTERN , 2048 , 1 , out );
  }

  /* sample data */
  Where = PW_Start_Address + 1092 + size1 + size2 + size3;
  fwrite ( &in_data[Where] , WholeSampleSize , 1 , out );

  /* crap */
  /*Crap ( "       PMD3       " , BAD , BAD , out );*/

/*  fflush ( DEBUG );*/
/*  fclose ( DEBUG );*/
  fflush ( out );
  fclose ( out );

  printf ( "done\n" );
  return;
}

