#ifndef ELI_PROCESS_GROUP_H_
#define ELI_PROCESS_GROUP_H_
#include "lua.h"

#ifdef _WIN32
#include <windows.h>

#define process_group_id HANDLE
#else
#include <unistd.h>

#define process_group_id pid_t
#endif

typedef struct process_group {
    int closed;

    process_group_id gid;
} process_group;

#define PROCESS_GROUP_METATABLE "ELI_PROCESS_GROUP"

void new_process_group(lua_State* L, process_group_id gid);

int process_group_create_meta(lua_State* L);
#endif