

#include <stdio.h>

void *xmalloc(int v)
{
    return malloc(v);
}

void *zfile_fopen(const char *name, const char *mode)
{
    return fopen(name, mode);
}
void zfile_fclose(void *z)
{
    fclose(z);
}
