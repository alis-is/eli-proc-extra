#ifndef ELI_PROCESS_H_
#define ELI_PROCESS_H_
#include "lua.h"
#include "stdioChannel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct process {
    int status;
    int isGroupLeader; // on windows it is job leader
    int isGroupMember; // on windows it is job member
#ifdef _WIN32
    int isSeparateProcessGroup;
    HANDLE hProcess;
    DWORD dwProcessId;
#else
    pid_t pid;
#endif
    stdioChannel* stdio[3];
} process;

#define PROCESS_METATABLE "ELI_PROCESS"

int process_create_meta(lua_State* L);
#endif