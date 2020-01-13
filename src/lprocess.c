#include "lua.h"
#include "lauxlib.h"
#include "lutil.h"
#include "lprocess.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* proc -- pid */
static int process_pid(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
#ifdef _WIN32
    lua_pushnumber(L, p->dwProcessId);
#else
    lua_pushnumber(L, p->pid);
#endif
    return 1;
}

/* proc -- exitcode/nil error */
static int process_wait(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    lua_Number interval = luaL_optnumber(L, 2, 0);
    lua_Number units = luaL_optnumber(L, 3, 1);
    if (p->status == -1)
    {
#ifdef _WIN32
        DWORD exitcode;
        if (WAIT_FAILED == WaitForSingleObject(p->hProcess, interval <= 0 ? INFINITE : (1e3 * interval / units)) || !GetExitCodeProcess(p->hProcess, &exitcode))
            return windows_pushlasterror(L);
        p->status = exitcode;
#else
        int status = 0;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (interval > 0)
        {
            int elapsed = 0;
            while (p->status == -1 && elapsed < interval)
            {
                int res = waitpid(p->pid, &status, WNOHANG);
                if (p->pid == res)
                {
                    p->status = WEXITSTATUS(status);
                    break;
                }
                else if (res == -1)
                {
                    p->status = 0;
                    break;
                }
                // res == 0 means process is still running
                usleep(1e6 / units);
                elapsed++;
            }
            if (p->status != -1)
            {
                p->status = WEXITSTATUS(status);
            }
        }
        else
        {
            int status;
            if (-1 == waitpid(p->pid, &status, 0))
                return push_error(L, NULL);
            p->status = WEXITSTATUS(status);
        }
#endif
    }
    lua_pushinteger(L, p->status);
    return 1;
}

/* proc -- exitcode/nil error */
static int process_kill(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    int signal = lua_gettop(L) > 1 && lua_toboolean(L, 2) ? SIGKILL : SIGTERM;

    if (p->status == -1)
    {
#ifdef _WIN32
        if (!TerminateProcess(p->hProcess, 0))
            return windows_pushlasterror(L);
        p->status = 0;
#else
        int const status = kill(p->pid, signal);
        if (status == -1)
            return push_error(L, NULL);
        p->status = WEXITSTATUS(status);
#endif
    }
    lua_pushnumber(L, p->status);
    return 1;
}

/* proc -- string */
static int process_tostring(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    char buf[40];
#ifdef _WIN32
    DWORD exitcode;
    if (!GetExitCodeProcess(p->hProcess, &exitcode))
        return windows_pushlasterror(L);
    p->status = (exitcode == STILL_ACTIVE) ? -1 : 0;
#else
    int status = 0;
    int res = waitpid(p->pid, &status, WNOHANG);
    if (p->pid == res)
        p->status = WEXITSTATUS(status);
    else if (res == -1)
        p->status = 0;
#endif
    lua_pushlstring(L, buf,
                    sprintf(buf, "process (%lu, %s)", (unsigned long)p->pid,
                            p->status == -1 ? "running" : "terminated"));
    return 1;
}

static int process_exitcode(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p->status == -1)
    {
#ifdef _WIN32
        DWORD exitcode;
        if (!GetExitCodeProcess(p->hProcess, &exitcode))
            return windows_pushlasterror(L);
        p->status = exitcode;
#else
        int status;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (res == -1)
            return push_error(L, NULL);
        if (res != 0)
            p->status = WEXITSTATUS(status);
#endif
    }
    lua_pushinteger(L, p->status);
    return 1;
}

static int process_exited(lua_State *L)
{
    struct process *p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    int active = 0;
    if (p->status == -1)
    {
#ifdef _WIN32
        DWORD exitcode;
        if (!GetExitCodeProcess(p->hProcess, &exitcode))
            return windows_pushlasterror(L);
        p->status = exitcode;
        active = exitcode == STILL_ACTIVE;
#else
        int status;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (res == -1)
            return push_error(L, NULL);
        if (res == 0)
            active = 1;
        else
            p->status = WEXITSTATUS(status);
#endif
    }
    lua_pushboolean(L, !active);
    return 1;
}

/*
** Creates process metatable.
*/
int process_create_meta(lua_State *L)
{
    luaL_newmetatable(L, PROCESS_METATABLE);

    /* Method table */
    lua_newtable(L);
    lua_pushcfunction(L, process_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, process_pid);
    lua_setfield(L, -2, "pid");
    lua_pushcfunction(L, process_wait);
    lua_setfield(L, -2, "wait");
    lua_pushcfunction(L, process_kill);
    lua_setfield(L, -2, "kill");
    lua_pushcfunction(L, process_exited);
    lua_setfield(L, -2, "exited");
    lua_pushcfunction(L, process_exitcode);
    lua_setfield(L, -2, "exitcode");

    /* Metamethods */
    lua_setfield(L, -2, "__index");
    return 1;
}
