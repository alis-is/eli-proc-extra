#ifndef ELI_PROCESS_H_
#define ELI_PROCESS_H_
#include "lua.h"
#include "stdioChannel.h"

#ifdef _WIN32
#include <windows.h>

#define process_id DWORD
#else
#include <unistd.h>

#define process_id pid_t
#endif

typedef struct process {
    int status;
    int signal;
#ifdef _WIN32
    int isChild;
    HANDLE hProcess;
#endif
    process_id pid;
    stdioChannel* stdio[3];
} process;

#define PROCESS_METATABLE "ELI_PROCESS"

int process_create_meta(lua_State* L);
#endif