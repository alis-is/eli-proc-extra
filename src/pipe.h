#include "lua.h"

typedef struct PIPE_DESCRIPTORS {
    int fd[2];
} PIPE_DESCRIPTORS;

int eli_pipe(lua_State* L);
int new_pipe(PIPE_DESCRIPTORS* descriptors);