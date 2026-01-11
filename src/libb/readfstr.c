#include	<u.h>
#include	<libc.h>
#include <b.h>

char*		
readfstr(char*f)
{
	long	l = 0;
	char*	s;

	s =  readf(f, nil, l, &l);
	if (s && strlen(s) != l){
		werrstr("binary file");
		free(s);
		return nil;
	}
	return s;
}

