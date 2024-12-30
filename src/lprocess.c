#include "lprocess.h"
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include "lauxlib.h"
#include "lerror.h"
#include "lsleep.h"
#include "lspawn.h"
#include "lstream.h"
#include "lua.h"
#include "lualib.h"
#include "stream.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* proc -- pid */
static int
process_pid(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p->status == -1) {
        lua_pushinteger(L, p->pid);
    } else {
        lua_pushinteger(L, -1);
    }
    return 1;
}
#ifndef _WIN32
static void
update_process_exit_status(process* p, int status) {
    if (WIFEXITED(status)) {
        p->status = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        p->signal = WTERMSIG(status);
        /* linux exit codes are in range 0 - 255 */
        p->status = 255 + p->signal;
    }
}
#endif
/* proc -- exitcode/nil error */
static int
process_wait(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    int duration = (int)luaL_optnumber(L, 2, 0);
    int divider = get_sleep_divider_from_state(L, 3, 1);
    if (p->status == -1) {
#ifdef _WIN32
        DWORD exitcode;
        if (WAIT_FAILED == WaitForSingleObject(p->hProcess, duration <= 0 ? INFINITE : (1e3 * duration / divider))
            || !GetExitCodeProcess(p->hProcess, &exitcode)) {
            return push_error(L, NULL);
        }
        p->status = exitcode;
#else
        int status = 0;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (p->pid == res) {
            update_process_exit_status(p, status);
        } else if (duration > 0) {
            int elapsed = 0;
            while (p->status == -1 && elapsed < duration) {
                int res = waitpid(p->pid, &status, WNOHANG);
                if (p->pid == res) {
                    update_process_exit_status(p, status);
                    break;
                } else if (res == -1) {
                    p->status = 0;
                    break;
                }
                // res == 0 means process is still running
                sleep_ms(sleep_duration_to_ms(1, divider));
                elapsed++;
            }
            if (p->status != -1) {
                update_process_exit_status(p, status);
            }
        } else {
            int status;
            if (-1 == waitpid(p->pid, &status, 0)) {
                return push_error(L, NULL);
            }
            update_process_exit_status(p, status);
        }
#endif
    }
    lua_pushinteger(L, p->status);
    lua_pushinteger(L, p->signal);
    return 2;
}

/* proc -- exitcode/nil error */
static int
process_kill(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    int signal = luaL_optnumber(L, 2, SIGTERM);

    if (p->status == -1) {
#ifdef _WIN32
        DWORD event = -1;
        switch (signal) {
            case SIGBREAK: event = CTRL_BREAK_EVENT; break;
        }
        if (event != -1) {
            if (!p->isChild) {
                return push_error(L,
                                  "it is possible to send SIGBREAK directly only to a spawned child process instance");
            }
            if (!GenerateConsoleCtrlEvent(event, p->pid)) {
                return push_error(L, NULL);
            }
            lua_pushboolean(L, 1);
            return 1;
        }
        if (signal != 9 /* SIGKILL*/) {
            return push_error(L, "on windows it is possible to send only SIGBREAK/SIGKILL signals to a process");
        }

        if (!TerminateProcess(p->hProcess, 1)) {
            return push_error(L, NULL);
        }
        //p->status = 0;
#else
        int const status = kill(p->pid, signal);
        if (status == -1) {
            return push_error(L, NULL);
        }
#endif
    }
    lua_pushboolean(L, 1);
    return 1;
}

/* proc -- string */
static int
process_tostring(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    char buf[50];
    if (p->status == -1) {
#ifdef _WIN32
        DWORD exitcode;
        if (!GetExitCodeProcess(p->hProcess, &exitcode)) {
            return push_error(L, NULL);
        }
        p->status = (exitcode == STILL_ACTIVE) ? -1 : 0;
#else
        int status = 0;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (p->pid == res) {
            update_process_exit_status(p, status);
        }
#endif
    }
    lua_pushlstring(
        L, buf, sprintf(buf, "process (%lu, %s)", (unsigned long)p->pid, p->status == -1 ? "running" : "terminated"));

    return 1;
}

static int
process_exitcode(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p->status == -1) {
#ifdef _WIN32
        DWORD exitcode;
        if (!GetExitCodeProcess(p->hProcess, &exitcode)) {
            return push_error(L, NULL);
        }
        p->status = exitcode;
#else
        int status;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (res == -1) {
            return push_error(L, NULL);
        }
        if (res != 0) {
            update_process_exit_status(p, status);
        }
#endif
    }
    lua_pushinteger(L, p->status);
    lua_pushinteger(L, p->signal);
    return 2;
}

static int
process_exited(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    int active = 0;
    if (p->status == -1) {
#ifdef _WIN32
        DWORD exitcode;
        if (!GetExitCodeProcess(p->hProcess, &exitcode)) {
            return push_error(L, NULL);
        }
        p->status = exitcode;
        active = exitcode == STILL_ACTIVE;
#else
        int status;
        int res = waitpid(p->pid, &status, WNOHANG);
        if (res == -1) {
            return push_error(L, NULL);
        }
        if (res == 0) {
            active = 1;
        } else {
            update_process_exit_status(p, status);
        }
#endif
    }
    lua_pushboolean(L, !active);
    return 1;
}

static int
process_get_stdin(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    stdio_channel* channel = p->stdio[STDIO_STDIN];
    if (channel == NULL) {
        lua_pushnil(L);
        return 1;
    }
    switch (channel->kind) {
        case STDIO_CHANNEL_STREAM_KIND:
        case STDIO_CHANNEL_EXTERNAL_STREAM_KIND:;
            ELI_STREAM* stream = eli_new_stream(L);
            stdio_channel_clone_into_stream(channel, stream);
            luaL_getmetatable(L, ELI_STREAM_W_METATABLE);
            lua_setmetatable(L, -2);
            break;
        default: lua_pushnil(L); break;
    }
    return 1;
}

static int
process_get_stdout(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    stdio_channel* channel = p->stdio[STDIO_STDOUT];
    if (channel == NULL) {
        lua_pushnil(L);
        return 1;
    }
    switch (channel->kind) {
        case STDIO_CHANNEL_STREAM_KIND:
        case STDIO_CHANNEL_EXTERNAL_STREAM_KIND:;
            ELI_STREAM* stream = eli_new_stream(L);
            stdio_channel_clone_into_stream(channel, stream);
            luaL_getmetatable(L, ELI_STREAM_R_METATABLE);
            lua_setmetatable(L, -2);
            break;
        case STDIO_CHANNEL_EXTERNAL_PATH_KIND:
            lua_pop(L, 1); // remove process from stack
            luaL_requiref(L, "io", luaopen_io, 0);
            lua_getfield(L, 1, "open");
            lua_replace(L, 1);
            lua_pushstring(L, channel->path);
            lua_pushstring(L, "r");
            lua_call(L, 2, LUA_MULTRET);
            return lua_gettop(L);
        default: lua_pushnil(L); break;
    }
    return 1;
}

static int
process_get_stderr(lua_State* L) {
    process* p = luaL_checkudata(L, 1, PROCESS_METATABLE);
    stdio_channel* channel = p->stdio[STDIO_STDERR];
    if (channel == NULL) {
        lua_pushnil(L);
        return 1;
    }
    switch (channel->kind) {
        case STDIO_CHANNEL_STREAM_KIND:
        case STDIO_CHANNEL_EXTERNAL_STREAM_KIND:;
            ELI_STREAM* stream = eli_new_stream(L);
            stdio_channel_clone_into_stream(channel, stream);
            luaL_getmetatable(L, ELI_STREAM_R_METATABLE);
            lua_setmetatable(L, -2);
            break;
        case STDIO_CHANNEL_EXTERNAL_PATH_KIND:
            lua_pop(L, 1); // remove process from stack
            luaL_requiref(L, "io", luaopen_io, 0);
            lua_getfield(L, 1, "open");
            lua_replace(L, 1);
            lua_pushstring(L, channel->path);
            lua_pushstring(L, "r");
            lua_call(L, 2, LUA_MULTRET);
            return lua_gettop(L);
        default: lua_pushnil(L); break;
    }
    return 1;
}

static const char*
get_channel_kind_alias(stdio_channel* channel) {
    if (channel == NULL) {
        return "closed";
    }
    switch (channel->kind) {
        case STDIO_CHANNEL_INHERIT_KIND: return "inherit";
        case STDIO_CHANNEL_STREAM_KIND: return "pipe";
        case STDIO_CHANNEL_EXTERNAL_STREAM_KIND: return "external";
        case STDIO_CHANNEL_EXTERNAL_PATH_KIND:
        case STDIO_CHANNEL_EXTERNAL_FILE_KIND: return "file";
        case STDIO_CHANNEL_IGNORE_KIND: return "ignore";
    }
}

static int
process_stdio_info(lua_State* L) {
    process* p = (process*)luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p == NULL) {
        return 0;
    }
    lua_newtable(L);
    lua_pushstring(L, get_channel_kind_alias(p->stdio[STDIO_STDIN]));
    lua_setfield(L, -2, "stdin");
    lua_pushstring(L, get_channel_kind_alias(p->stdio[STDIO_STDOUT]));
    lua_setfield(L, -2, "stdout");
    lua_pushstring(L, get_channel_kind_alias(p->stdio[STDIO_STDERR]));
    lua_setfield(L, -2, "stderr");
    return 1;
}

static int
process_get_group(lua_State* L) {
    process* p = (process*)luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p == NULL) {
        return 0;
    }
    // get from user value
    lua_getiuservalue(L, 1, 1);
    return 1;
}

static int
process_close(lua_State* L) {
    process* p = (process*)luaL_checkudata(L, 1, PROCESS_METATABLE);
    if (p == NULL) {
        return 0;
    }
    close_proc_stdio_channel(p, STDIO_STDIN);
    close_proc_stdio_channel(p, STDIO_STDOUT);
    close_proc_stdio_channel(p, STDIO_STDERR);
    return 0;
}

/*
** Creates process metatable.
*/
int
process_create_meta(lua_State* L) {
    luaopen_eli_stream_extra(L);
    luaL_newmetatable(L, PROCESS_METATABLE);

    /* Method table */
    lua_newtable(L);
    lua_pushcfunction(L, process_tostring);
    lua_setfield(L, -2, "__tostring");
    lua_pushcfunction(L, process_pid);
    lua_setfield(L, -2, "pid");
    lua_pushcfunction(L, process_pid);
    lua_setfield(L, -2, "get_pid");
    lua_pushcfunction(L, process_wait);
    lua_setfield(L, -2, "wait");
    lua_pushcfunction(L, process_kill);
    lua_setfield(L, -2, "kill");
    lua_pushcfunction(L, process_exited);
    lua_setfield(L, -2, "exited");
    lua_pushcfunction(L, process_exitcode);
    lua_setfield(L, -2, "get_exit_code");

    lua_pushcfunction(L, process_get_stdin);
    lua_setfield(L, -2, "get_stdin");
    lua_pushcfunction(L, process_get_stdout);
    lua_setfield(L, -2, "get_stdout");
    lua_pushcfunction(L, process_get_stderr);
    lua_setfield(L, -2, "get_stderr");
    lua_pushcfunction(L, process_stdio_info);
    lua_setfield(L, -2, "get_stdio_info");
    lua_pushcfunction(L, process_get_group);
    lua_setfield(L, -2, "get_group");

    lua_pushstring(L, PROCESS_METATABLE);
    lua_setfield(L, -2, "__type");
    /* Metamethods */
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, process_close);
    lua_setfield(L, -2, "__gc");
    lua_pushcfunction(L, process_close);
    lua_setfield(L, -2, "__close");
    return 1;
}
