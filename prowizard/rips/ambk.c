#ifdef DOS
#include "..\include\globals.h"
#include "..\include\extern.h"
#endif

#ifdef UNIX
#include "../include/globals.h"
#include "../include/extern.h"
#endif


void Rip_AmBk ( void )
{
  /* PW_k is already the whole file size */

  OutputSize = PW_k;

  CONVERT = GOOD;
  Save_Rip ( "AMOS Bank (AmBk)", AmBk );
  
  if ( Save_Status == GOOD )
    PW_i += 4;
}

