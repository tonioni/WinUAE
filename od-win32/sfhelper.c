
#include <stdio.h>
#include <stdlib.h>

#include "sysconfig.h"
#include "sysdeps.h"

int main (int argc, char **argv)
{
    char *wu;
    FILE *f;
    uae_u8 *b, *p1, *p2, *p3, *cfgbuf, *databuf;
    int size, size2, i, num, offset;

    if (argc < 4) {
	printf("Usage: sfhelper.exe <winuae.exe> <config file> <file1> [<file2>..]");
	return 0;
    }
    wu = argv[1];
    f = fopen(wu,"rb");
    if (!f) {
	printf("Couldn't open '%s'\n", wu);
	return 0;
    }
    fseek (f, 0, SEEK_END);
    size = ftell(f);
    fseek (f, 0, SEEK_SET);
    b = malloc (size);
    if (!b) {
	printf ("out of memory, can't allocate %d bytes\n", size);
	return 0;
    }
    fread (b, size, 1, f);
    fclose (f);

    cfgbuf = databuf = 0;
    p1 = b;
    while (p1 < b + size) {
	if (*p1 == '_') {
	    if (!strcmp (p1, "_CONFIG_STARTS_HERE"))
		cfgbuf = p1;
	    if (!strcmp (p1, "_DATA_STARTS_HERE"))
		databuf = p1;
	}
	p1++;
	if (cfgbuf && databuf)
	    break;
    }

    if (!cfgbuf || !databuf) {
	printf ("couldn't find preallocated data buffer");
	return 0;
    }

    while (*cfgbuf++);
    printf ("processing config file...\n");
    f = fopen(argv[2],"rb");
    if (!f) {
	printf ("Couldn't open config file '%s'\n", argv[2]);
	return 0;
    }
    fread (cfgbuf, 1, 50000, f);
    fclose (f);
    printf ("done\n");

    num = argc - 3;
    offset = num * 256;
    p1 = databuf + offset;
    p2 = databuf;
    while (*p2++);
    memset (p2, 0, offset);
    *p2++ = offset >> 24;
    *p2++ = offset >> 16;
    *p2++ = offset >> 8;
    *p2++ = offset >> 0;
    for (i = 0; i < num; i++) {
	printf ("processing '%s'\n", argv[i + 3]);
	f = fopen(argv[i + 3], "rb");
	if (!f) {
	    printf ("Couldn't open '%s'\n", argv[i + 3]);
	    return 0;
	}
	fseek (f, 0, SEEK_END);
	size2 = ftell (f);
	fseek (f, 0, SEEK_SET);
	fread (p1, 1, size2, f);
	fclose (f);
	*p2++ = size2 >> 24;
	*p2++ = size2 >> 16;
	*p2++ = size2 >> 8;
	*p2++ = size2 >> 0;
	p3 = argv[i + 3] + strlen (argv[i + 3]) - 1;
	while (p3 > argv[i + 3] && *p3 != '/' && *p3 != '\\') p3--;
	if (p3 > argv[i + 3])
	    p3++;
	strcpy (p2, p3);
	p2 += strlen (p2) + 1;
	printf ("saved as '%s'\n", p3);
    }
    printf ("Writing updated '%s'\n", wu);
    f = fopen(wu,"wb");
    if (!f) {
	printf("Couldn't open '%s' for writing\n", wu);
	return 0;
    }
    fwrite (b, 1, size, f);
    fclose (f);
    printf ("done\n");
    return 0;
}












