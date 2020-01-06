#ifdef __APPLE__

#include <crt_externs.h>
#define environ (*_NSGetEnviron())

#else

extern char **environ;

#endif

