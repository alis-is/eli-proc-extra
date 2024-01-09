#include "lprocess_group.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"
#include "lspawn.h"
#include "lstream.h"
#include "lua.h"
#include "lualib.h"
#include "lutil.h"
#include "stream.h"

#ifdef _WIN32
#include <windows.h>
#include "kill.h"

DWORD
process_group_generate_ctrl_event(lua_State* L, DWORD dwProcessId, DWORD signal) {
    // create temp file and write kill binary into it
    // run kill binary with pid and signal

    char path[MAX_PATH];
    if (GetTempPath(MAX_PATH, path) == 0) {
        // Handle error: Failed to retrieve temporary path
        return 0;
    }

    if (GetTempFileName(path, "kill", 0, NULL) == 0) {
        // Handle error: Failed to generate temporary file name
        return 0;
    }

    // Append ".exe" extension to the generated temporary file path
    size_t pathLength = strlen(path);
    size_t extensionLength = strlen(".exe");
    if (pathLength + extensionLength >= MAX_PATH) {
        // Adjust the path to fit the extension
        path[MAX_PATH - extensionLength - 1] = '\0';
    }
    if (strcat(path, ".exe") == NULL) {
        // Handle error: Failed to append extension to path
        return 0;
    }

    HANDLE exeFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (exeFile == INVALID_HANDLE_VALUE) {
        return 0;
    }

    unsigned long size = 0;
    DWORD written = 0;
    if (WriteFile(exeFile, killBinary, KILL_BINARY_SIZE, &written, NULL) == 0 || written != KILL_BINARY_SIZE) {
        CloseHandle(exeFile);
        return 0;
    }

    DWORD exitCode = -1;
    char commandLine[40];
    sprintf(commandLine, "%lu %lu", (unsigned long)dwProcessId, (unsigned long)signal);
    PROCESS_INFORMATION pi;

    STARTUPINFO si;
    if (CreateProcess(path, commandLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi) == 0) {
        CloseHandle(exeFile);
        return 0;
    }
    CloseHandle(exeFile);
    if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, &exitCode)) {
        CloseHandle(pi.hProcess);
        return 0;
    }
    CloseHandle(pi.hProcess);

    if (exitCode == 0) {
        return 1;
    }
    return 0;
}

#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef _WIN32
void
new_process_group(lua_State* L, HANDLE hJob) {
#else
void
new_process_group(lua_State* L, pid_t gpid) {
#endif
    process_group* pg = lua_newuserdatauv(L, sizeof(process_group), 1); // process-group
    memset(pg, 0, sizeof(process_group));
    luaL_getmetatable(L, PROCESS_GROUP_METATABLE); // process-group metatable
    lua_setmetatable(L, -2);                       // process-group
    pg->closed = 0;
    // new table to store processes
    lua_newtable(L);             // process-group process-table
    lua_setiuservalue(L, -2, 1); // Store the process-table in the first uv slot of process-group

#ifdef _WIN32
    pg->hJob = hJob;
#else
    pg->gpid = gpid;
#endif
}

static int
process_group_tostring(lua_State* L) {
    process_group* p = luaL_checkudata(L, 1, PROCESS_GROUP_METATABLE);
    char buf[40];
#ifdef _WIN32
    lua_pushlstring(L, buf, sprintf(buf, "process group (%p)", (void*)(uintptr_t)p->hJob));
#else
    lua_pushlstring(L, buf, sprintf(buf, "process group (%lu)", (unsigned long)p->gpid));
#endif
    return 1;
}

/* proc -- exitcode/nil error */
static int
process_group_kill(lua_State* L) {
    process_group* p = luaL_checkudata(L, 1, PROCESS_GROUP_METATABLE);
    int signal = luaL_optnumber(L, 2, SIGTERM);

#ifdef _WIN32
    DWORD event = -1;
    switch (signal) {
        case SIGINT: event = CTRL_C_EVENT; break;
        case SIGBREAK: event = CTRL_BREAK_EVENT; break;
    }
    if (event != -1) {
        // get from user value
        lua_getiuservalue(L, 1, 1); // process-group process-table
        // iterate over all processes in the group
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            // call kill on each process
            process* proc = (process*)luaL_testudata(L, -1, PROCESS_METATABLE); // key, proc/nil
            if (proc == NULL) {
                lua_pop(L, 1);
                continue;
            }

            if (!process_group_generate_ctrl_event(L, proc->dwProcessId, event)) {
                return push_error(L, NULL);
            }
        }
        return 0;
    }
    if (signal != 9) {
        return push_error(L,
                          "on windows it is possible to send only SIGINT/SIGBREAK/SIGKILL signals to a process group");
    }
    if (p->hJob == NULL) {          // iterate and terminate directly
        lua_getiuservalue(L, 1, 1); // process-group process-table
        // iterate over all processes in the group
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            // call kill on each process
            process* proc = (process*)luaL_testudata(L, -1, PROCESS_METATABLE); // key, proc/nil
            if (proc == NULL) {
                lua_pop(L, 1);
                continue;
            }
            if (!TerminateProcess(proc->hProcess, 1)) {
                return windows_pushlasterror(L);
            }
        }
        return 0;
    }
    if (!TerminateJobObject(p->hJob, 1)) {
        return windows_pushlasterror(L);
    }
#else
    int const status = kill(-p->gpid, signal);
    if (status == -1) {
        return push_error(L, NULL);
    }
#endif
    return 0;
}

static int
process_group_join(lua_State* L) {
    process_group* pg = luaL_checkudata(L, 1, PROCESS_GROUP_METATABLE);
    process* p = luaL_checkudata(L, 2, PROCESS_METATABLE);
    if (pg != NULL && p != NULL) {
        lua_getiuservalue(L, 1, 1);
        // append process to the process table
        lua_pushvalue(L, -2);
    }
    return 0;
}

static int
process_group_close(lua_State* L) {
    process_group* p = (process_group*)luaL_checkudata(L, 1, PROCESS_GROUP_METATABLE);
    if (p->closed == 0) {
#ifdef _WIN32
        CloseHandle(p->hJob);
#endif
        p->closed = 1;
    }
    return 0;
}

/*
** Creates process metatable.
*/
int
process_group_create_meta(lua_State* L) {
    luaopen_eli_stream_extra(L);
    luaL_newmetatable(L, PROCESS_GROUP_METATABLE);

    /* Method table */
    lua_newtable(L);
    lua_pushcfunction(L, process_group_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, process_group_kill);
    lua_setfield(L, -2, "kill");
    lua_pushcfunction(L, process_group_join);
    lua_setfield(L, -2, "__join");

    lua_pushstring(L, PROCESS_GROUP_METATABLE);
    lua_setfield(L, -2, "__type");
    /* Metamethods */
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, process_group_close);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, process_group_close);
    lua_setfield(L, -2, "__close");
    return 1;
}
