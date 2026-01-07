#define _CRT_RAND_S
#include "lprocess_group.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"
#include "lerror.h"
#include "lspawn.h"
#include "lstream.h"
#include "lua.h"
#include "lualib.h"
#include "stream.h"

#ifdef _WIN32
#include <windows.h>
#include "kill.h"
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef _WIN32
static wchar_t cachedHelperPath[MAX_PATH] = {0};

/*
** Generates a random filename and attempts to create it exclusively.
** Returns 1 on success, 0 on failure.
*/
static int
ensure_helper_binary(void) {
    if (cachedHelperPath[0] != L'\0') {
        return 1; // Already initialized
    }

    wchar_t tempDir[MAX_PATH];
    if (GetTempPathW(MAX_PATH, tempDir) == 0) {
        return 0;
    }

    // Try up to 5 times to generate a unique file (in case of collision)
    for (int i = 0; i < 5; i++) {
        unsigned int rnd1, rnd2;
        if (rand_s(&rnd1) != 0 || rand_s(&rnd2) != 0) {
            return 0; // Random generation failed
        }

        // Generate a filename with 64-bits of randomness
        // e.g., C:\Temp\eli_kill_a1b2c3d4_e5f6g7h8.exe
        wchar_t candidatePath[MAX_PATH];
        swprintf(candidatePath, MAX_PATH, L"%ls%ls_%08x_%08x.exe", tempDir, L"eli_kill", rnd1, rnd2);

        // CREATE_NEW is critical here.
        // It fails if the file already exists (preventing squatting/overwriting).
        HANDLE hFile = CreateFileW(candidatePath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);

        if (hFile != INVALID_HANDLE_VALUE) {
            // We successfully reserved a unique name that didn't exist before.
            DWORD written = 0;
            int writeResult = WriteFile(hFile, killBinary, KILL_BINARY_SIZE, &written, NULL);
            CloseHandle(hFile);

            if (writeResult && written == KILL_BINARY_SIZE) {
                // Success! Cache the path and return.
                wcscpy(cachedHelperPath, candidatePath);
                return 1;
            } else {
                // Write failed (disk full?), clean up and fail.
                DeleteFileW(candidatePath);
                return 0;
            }
        }

        // If we are here, CreateFile failed.
        // If ERROR_FILE_EXISTS, we loop and try a new random number.
        if (GetLastError() != ERROR_FILE_EXISTS) {
            return 0; // Genuine IO error
        }
    }

    return 0; // Failed to generate unique name after retries
}

DWORD
process_group_generate_ctrl_event(lua_State* L, DWORD* pid, int pidc, DWORD signal) {
    // Ensure the binary exists
    if (!ensure_helper_binary()) {
        return 0;
    }

    // Allocate space for PIDs + Signal + Safety
    wchar_t* commandLine = malloc(sizeof(wchar_t) * 12 * (pidc + 2));
    if (commandLine == NULL) {
        return 0;
    }
    commandLine[0] = L'\0';

    // Build arguments: "PID1 PID2 ... PIDN SIGNAL"
    for (int i = 0; i < pidc; i++) {
        wchar_t temp[12];
        swprintf(temp, 12, L"%lu ", (unsigned long)pid[i]);
        wcscat(commandLine, temp);
    }
    wchar_t temp[12];
    swprintf(temp, 12, L"%lu", (unsigned long)signal);
    wcscat(commandLine, temp);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    int created = 0;
    int retries = 5;
    while (retries > 0) {
        // Create Process using the cached path
        if (CreateProcessW(cachedHelperPath, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)
            == 0) {
            free(commandLine);
            return 0;
        }
        DWORD err = GetLastError();
        // If file is locked by AV or OS (Access Denied / Sharing Violation), wait and retry
        if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) {
            Sleep(100); // Wait 100ms before retrying
            retries--;
        } else {
            break; // Genuine error, fail immediately
        }
    }
    free(commandLine);
    if (!created) {
        return 0;
    }

    // Wait for completion
    DWORD exitCode = -1;
    int failed =
        WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (failed || exitCode != 0) {
        return 0;
    }
    return 1;
}
#endif

void
new_process_group(lua_State* L, process_group_id gid) {
    process_group* pg = lua_newuserdatauv(L, sizeof(process_group), 1); // process-group
    memset(pg, 0, sizeof(process_group));
    luaL_getmetatable(L, PROCESS_GROUP_METATABLE); // process-group metatable
    lua_setmetatable(L, -2);                       // process-group
    pg->closed = 0;
    // new table to store processes
    lua_newtable(L);             // process-group process-table
    lua_setiuservalue(L, -2, 1); // Store the process-table in the first uv slot of process-group

    pg->gid = gid;
}

static int
process_group_tostring(lua_State* L) {
    process_group* p = luaL_checkudata(L, 1, PROCESS_GROUP_METATABLE);
    char buf[40];
    lua_pushlstring(L, buf, sprintf(buf, "process group (%llu)", (unsigned long long)p->gid));
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
        // get length of process table
        int length = (int)lua_rawlen(L, -1);
        lua_pushnil(L);

        DWORD* pids = malloc(sizeof(DWORD) * length);

        int index = 0;
        while (lua_next(L, -2) != 0) {
            // call kill on each process
            process* proc = (process*)luaL_testudata(L, -1, PROCESS_METATABLE); // key, proc/nil
            lua_pop(L, 1);
            if (proc == NULL) {
                continue;
            }
            pids[index++] = proc->pid;
        }
        if (process_group_generate_ctrl_event(L, pids, length, event) == 0) {
            return push_error(L, NULL);
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    if (signal != 9) {
        return push_error(L,
                          "on windows it is possible to send only SIGINT/SIGBREAK/SIGKILL signals to a process group");
    }
    if (p->gid == NULL) {           // iterate and terminate directly
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
                return push_error(L, NULL);
            }
            lua_pop(L, 1);
        }
        lua_pushboolean(L, 1);
        return 1;
    }
    if (!TerminateJobObject(p->gid, 1)) {
        return push_error(L, NULL);
    }
#else
    int const status = kill(-p->gid, signal);
    if (status == -1) {
        return push_error(L, NULL);
    }
#endif
    lua_pushboolean(L, 1);
    return 1;
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
        CloseHandle(p->gid);
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
