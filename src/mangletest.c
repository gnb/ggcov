#include <stdio.h>
#include "demangle.h"

const char *argv0;

int
main(int argc, char **argv)
{
    enum { DEMANGLE, NORMALISE } mode = DEMANGLE;
    int i;

    argv0 = argv[0];
    
    for (i = 1 ; i < argc ; i++)
    {
    	if (!strcmp(argv[i], "--demangle"))
	{
	    mode = DEMANGLE;
	}
	else if (!strcmp(argv[i], "--normalise"))
	{
	    mode = NORMALISE;
	}
	else if (argv[i][0] == '-')
	{
	    fprintf(stderr, "Usage: mangletest [--demangle|--normalise] sym...\n");
	    exit(1);
	}
	else
	{
	    char *res;

	    if (mode == DEMANGLE)
	    {
	    	res = demangle(argv[i]);
		printf("demangle(\"%s\") =\n\"%s\"\n",
		    argv[i],
		    (res == 0 ? "(null)" : res));
	    }
	    else
	    {
	    	res = normalise_mangled(argv[i]);
		printf("normalise_mangled(\"%s\") =\n\"%s\"\n",
		    argv[i],
		    (res == 0 ? "(null)" : res));
	    }
	    if (res)
	    	g_free(res);
	}
    }
    
    return 0;
}

