#ifndef ELI_PROCESS_GROUP_H_
#define ELI_PROCESS_GROUP_H_
#include "lua.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct process_group {
    int closed;
#ifdef _WIN32
    HANDLE hJob;
#else
    pid_t gpid;
#endif
} process_group;

#define PROCESS_GROUP_METATABLE "ELI_PROCESS_GROUP"

#ifdef _WIN32
void new_process_group(lua_State* L, HANDLE hJob);
#else
void new_process_group(lua_State* L, pid_t gpid);
#endif
int process_group_create_meta(lua_State* L);
#endif