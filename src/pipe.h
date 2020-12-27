#include "lua.h"

typedef struct ELI_PIPE_END
{
#ifdef _WIN32
    HANDLE h;
#else
    int fd;
#endif
    int closed;
    int nonblocking;
    const char * mode;
} ELI_PIPE_END;

#define PIPE_METATABLE "ELI_PIPE_END"

typedef struct PIPE_DESCRIPTORS {
#ifdef _WIN32
    HANDLE ph[2];
#else
    int fd[2];
#endif
} PIPE_DESCRIPTORS;

int eli_pipe(lua_State *L);
int pipe_create_meta(lua_State *L);
int new_pipe(PIPE_DESCRIPTORS * descriptors);