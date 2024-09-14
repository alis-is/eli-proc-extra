#include "lauxlib.h"
#include "lua.h"

#include <signal.h>
#include "lprocess.h"
#include "lspawn.h"
#include "lutil.h"
#include "pipe.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <windows.h>

#define open             _open
#define RDONLY_FLAG      _O_RDONLY
#define WRONLY_FLAG      _O_WRONLY | _O_TRUNC | _O_BINARY | _O_CREAT
#define CREATION_FLAG    _S_IREAD | _S_IWRITE
#define SLEEP_MULTIPLIER 1e3
#else
#include <fcntl.h>
#include <unistd.h>

#define RDONLY_FLAG      O_RDONLY
#define WRONLY_FLAG      O_WRONLY | O_TRUNC | O_CREAT
#define CREATION_FLAG    S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
#define SLEEP_MULTIPLIER 1e6
#endif

/* REDIRECT KINDS */
#define IGNORE  0
#define INHERIT 1
#define PIPE    2
#define PATH    3

/* END REDIRECT KINDS */

static int
lcheck_option_with_fallback(lua_State* L, int arg, const char* def, const char* fallback, const char* const lst[]) {
    const char* name = (def) ? luaL_optstring(L, arg, def) : luaL_checkstring(L, arg);
    int i;
    int j = -1;
    for (i = 0; lst[i]; i++) {
        if (strcmp(lst[i], name) == 0) {
            return i;
        }
        if (fallback && strcmp(lst[i], fallback) == 0) {
            j = i;
        }
    }
    if (j > -1) {
        return j;
    }

    return luaL_argerror(L, arg, lua_pushfstring(L, "invalid option '%s'", name));
}

static int
setup_redirect(lua_State* L, const char* stdname, int idx, spawn_params* p) {
    stdioChannel* channel = calloc(1, sizeof(stdioChannel));
    channel->fdToClose = -1;
    lua_getfield(L, idx, stdname);

    int stdioKind;

    switch (stdname[3]) {
        case 'i': stdioKind = STDIO_STDIN; break;
        case 'o': stdioKind = STDIO_STDOUT; break;
        case 'e': stdioKind = STDIO_STDERR; break;
        case 'p': stdioKind = STDIO_OUTPUT_STREAMS; break; // output
    }

    int top = lua_gettop(L);
    switch (lua_type(L, -1)) {
        case LUA_TNIL: // fall through
        case LUA_TSTRING:;
            static const char* lst[] = {"ignore", "inherit", "pipe", "path", NULL};
            int kind = lcheck_option_with_fallback(L, -1, "pipe", "path", lst); // fallback to default pipe mode
            switch (kind) {
                case IGNORE: channel->kind = STDIO_CHANNEL_IGNORE_KIND; break;
                case INHERIT: channel->kind = STDIO_CHANNEL_INHERIT_KIND;
#ifdef _WIN32
                    spawn_param_redirect(p, stdioKind,
                                         GetStdHandle(-10 + (-1 * stdioKind) /* remap stdio kind to win STD_*/));
#else
                    spawn_param_redirect(p, stdioKind, stdioKind);
#endif
                    break;
                case PATH:
                    channel->kind = STDIO_CHANNEL_EXTERNAL_PATH_KIND;
                    const char* path = luaL_checkstring(L, -1);
                    channel->path = path;
                    int fd;
                    if (stdioKind == STDIO_STDIN) {
                        if ((fd = open(path, RDONLY_FLAG)) == -1) {
                            return push_error(L, "Failed to open stdin file!");
                        }
                    } else {
                        if ((fd = open(path, WRONLY_FLAG, CREATION_FLAG)) == -1) {
                            return push_error(L, "Failed to create stdout/stderr file!");
                        }
                    }
#ifdef _WIN32
                    spawn_param_redirect(p, stdioKind, (HANDLE)_get_osfhandle(fd));
#else
                    spawn_param_redirect(p, stdioKind, fd);
#endif
                    break;
                case PIPE:
                    channel->kind = STDIO_CHANNEL_STREAM_KIND;
                    PIPE_DESCRIPTORS descriptors;
                    if (new_pipe(&descriptors) == -1) {
                        return push_error(L, "Failed to create pipe!");
                    };
                    ELI_STREAM* stream = eli_new_stream(NULL);
                    stream->fd = descriptors.fd[stdioKind == STDIO_STDIN ? 1 : 0];
                    channel->stream = stream;
#ifdef _WIN32
                    spawn_param_redirect(p, stdioKind,
                                         (HANDLE)_get_osfhandle(descriptors.fd[stdioKind == STDIO_STDIN ? 0 : 1]));
#else
                    spawn_param_redirect(p, stdioKind, descriptors.fd[stdioKind == STDIO_STDIN ? 0 : 1]);
#endif
                    channel->fdToClose = descriptors.fd[stdioKind == STDIO_STDIN ? 0 : 1];
                    break;
                default: luaL_error(L, "Invalid stdio type: %s!"); return 1;
            }
            break;
        case LUA_TUSERDATA:
            lua_getmetatable(L, idx);
            luaL_getmetatable(L, LUA_FILEHANDLE);
            luaL_getmetatable(L, ELI_STREAM_RW_METATABLE);
            switch (stdioKind) {
                case STDIO_STDIN: luaL_getmetatable(L, ELI_STREAM_W_METATABLE); break;
                case STDIO_OUTPUT_STREAMS:
                case STDIO_STDOUT:
                case STDIO_STDERR: luaL_getmetatable(L, ELI_STREAM_R_METATABLE); break;
            }

            if (lua_rawequal(L, -3, -4)) { // file
                lua_pop(L, lua_gettop(L) - top);
                luaL_Stream* fh = (luaL_Stream*)luaL_checkudata(L, -1, "FILE*");
                if (fh == NULL) {
                    luaL_error(L, "%s: invalid file");
                    return 1;
                }
                if (fh->closef == 0 || fh->f == NULL) {
                    luaL_error(L, "%s: closed file");
                    return 1;
                }

                channel->kind = STDIO_CHANNEL_EXTERNAL_FILE_KIND;
                channel->file = fh;
#ifdef _WIN32
                spawn_param_redirect(p, stdioKind, (HANDLE)_get_osfhandle(_fileno(fh->f)));
#else
                spawn_param_redirect(p, stdioKind, fileno(fh->f));
#endif
            } else if (lua_rawequal(L, -1, -4) || lua_rawequal(L, -2, -4)) { // eli pipe
                lua_pop(L, lua_gettop(L) - top);
                ELI_STREAM* stream = (ELI_STREAM*)lua_touserdata(L, -1);
                if (stream == NULL) {
                    luaL_error(L, "%s: invalid stream");
                    return 1;
                }
                if (stream->closed) {
                    luaL_error(L, "%s: closed pipe");
                    return 1;
                }

                channel->kind = STDIO_CHANNEL_EXTERNAL_STREAM_KIND;
                channel->stream = stream;
#ifdef _WIN32
                spawn_param_redirect(p, stdioKind, (HANDLE)_get_osfhandle(stream->fd));
#else
                spawn_param_redirect(p, stdioKind, stream->fd);
#endif
            } else {
                lua_pop(L, lua_gettop(L) - top);
                luaL_typeerror(L, -1, "FILE*/ELI_STREAM");
                return 1;
            }
    }

    switch (stdioKind) {
        case STDIO_OUTPUT_STREAMS:
            p->stdio[STDIO_STDOUT] = channel;
            p->stdio[STDIO_STDERR] = channel;
            break;
        default: p->stdio[stdioKind] = channel; break;
    }
    lua_pop(L, 1);
    return 0;
}

static int
setup_redirects(lua_State* L, int idx, spawn_params* p) {
    lua_getfield(L, idx, "stdio");

    // pipe, inherit, ignore are supported values
    switch (lua_type(L, -1)) {
        default: luaL_error(L, "bad args option (table expected, got %s)", luaL_typename(L, -1)); return 1;
        case LUA_TNIL:
            // fall through
        case LUA_TSTRING:;
            static const char* lst[] = {"ignore", "inherit", "pipe", NULL};
            luaL_checkoption(L, -1, "pipe", lst); // force only limited set of strings
            const char* stdio = luaL_optstring(L, -1, "pipe");
            lua_pop(L, 1);
            // rebuild string to table
            lua_newtable(L);
            lua_pushstring(L, stdio);
            lua_setfield(L, -2, "stdin");
            lua_pushstring(L, stdio);
            lua_setfield(L, -2, "stdout");
            lua_pushstring(L, stdio);
            lua_setfield(L, -2, "stderr");
            break;
        case LUA_TTABLE: break;
    }

    int res;
    res = setup_redirect(L, "stdin", -1, p);
    if (res) {
        return res;
    }

    int wantsCombinedOutput = lua_getfield(L, -1, "output") != LUA_TNIL;
    lua_pop(L, 1);
    if (wantsCombinedOutput) {
        int expectsStdout = lua_getfield(L, -1, "stdout") != LUA_TNIL;
        lua_pop(L, 1);
        int expectsStderr = lua_getfield(L, -1, "stderr") != LUA_TNIL;
        lua_pop(L, 1);
        if (expectsStdout || expectsStderr) {
            luaL_error(L, "cannot specify both the output option and stdout/stderr options");
            return 1;
        }
        res = setup_redirect(L, "output", -1, p);
        if (res) {
            return res;
        }
    } else {
        res = setup_redirect(L, "stdout", -1, p);
        if (res) {
            return res;
        }
        res = setup_redirect(L, "stderr", -1, p);
        if (res) {
            return res;
        }
    }

    lua_pop(L, 1);
    return 0;
}

/* filename [args, opts] -- proc/nil error */
/* args-opts -- proc/nil error */
static int
eli_spawn(lua_State* L) {
    spawn_params* params;
    process_group* pg = NULL;
    int have_options;
    switch (lua_type(L, 1)) {
        default: return luaL_typeerror(L, 1, "string or table");
        case LUA_TSTRING:
            switch (lua_type(L, 2)) {
                default: return luaL_typeerror(L, 2, "table");
                case LUA_TNONE: have_options = 0; break;
                case LUA_TTABLE: have_options = 1; break;
            }
            break;
        case LUA_TTABLE:
            have_options = 1;
            lua_getfield(L, 1, "command"); /* opts ... cmd */
            if (!lua_isnil(L, -1)) {
                /* convert {command=command,arg1,...} to command {arg1,...} */
                lua_insert(L, 1); /* cmd opts ... */
            } else {
                /* convert {arg0,arg1,...} to arg0 {arg1,...} */
                size_t i, n = lua_rawlen(L, 1);
                lua_rawgeti(L, 1, 1); /* opts ... nil cmd */
                lua_insert(L, 1);     /* cmd opts ... nil */
                for (i = 2; i <= n; i++) {
                    lua_rawgeti(L, 2, i);     /* cmd opts ... nil argi */
                    lua_rawseti(L, 2, i - 1); /* cmd opts ... nil */
                }
                lua_rawseti(L, 2, n); /* cmd opts ... */
            }
            if (lua_type(L, 1) != LUA_TSTRING) {
                return luaL_error(L, "bad command option (string expected, got %s)", luaL_typename(L, 1));
            }
            break;
    }

    params = spawn_param_init(L);
    /* get filename to execute */
    spawn_param_filename(params, lua_tostring(L, 1));
    /* get arguments, environment, and redirections */
    if (have_options) {
        // newProcessGroup
        lua_getfield(L, 2, "createProcessGroup"); /* cmd opts ... createProcessGroup */
        if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) {
            params->createProcessGroup = 1;
        }
        lua_pop(L, 1); /* cmd opts ... */

        lua_getfield(L, 2, "username"); /* cmd opts ... createProcessGroup */
        if (lua_type(L, -1) == LUA_TSTRING) {
            params->username = lua_tostring(L, -1);
        }
        lua_pop(L, 1); /* cmd opts ... */

        lua_getfield(L, 2, "password"); /* cmd opts ... createProcessGroup */
        if (lua_type(L, -1) == LUA_TSTRING) {
            params->password = lua_tostring(L, -1);
        }
        lua_pop(L, 1); /* cmd opts ... */

        // options
        lua_getfield(L, 2, "args"); /* cmd opts ... argtab */
        switch (lua_type(L, -1)) {
            default: return luaL_error(L, "bad args option (table expected, got %s)", luaL_typename(L, -1));
            case LUA_TNIL:
                lua_pop(L, 1);       /* cmd opts ... */
                lua_pushvalue(L, 2); /* cmd opts ... opts */
                                     /*FALLTHRU*/
            case LUA_TTABLE:
                if (lua_rawlen(L, 2) > 0) {
                    return luaL_error(L, "cannot specify both the args option and array values");
                }
                spawn_param_args(L, params); /* cmd opts ... */
                break;
        }
        lua_pop(L, 1); /* cmd opts ... */

        // env
        lua_getfield(L, 2, "env"); /* cmd opts ... envtab */
        switch (lua_type(L, -1)) {
            default: return luaL_error(L, "bad env option (table expected, got %s)", luaL_typename(L, -1));
            case LUA_TNIL: break;
            case LUA_TTABLE:
                spawn_param_env(L, params);
                /* cmd opts ... */
                break;
        }
        lua_pop(L, 1);
    }
    int err_count = setup_redirects(L, 2, params); /* cmd opts ... */
    if (err_count > 0) {
        return err_count;
    }
    // keep just params and process group at the stack
    lua_getfield(L, 2, "processGroup"); /* -> cmd opts params processGroup/nil */
    lua_rotate(L, 1, 2);                /* -> params processGroup/nil cmd opts */
    lua_settop(L, 2);                   /* -> params processGroup/nil */
    return spawn_param_execute(L);      /* proc/nil error */
}

static int
eli_get_process_by_id(lua_State* L) {
    int pid = luaL_checkinteger(L, 1);
    process* p = lua_newuserdatauv(L, sizeof(process), 1);
    if (p == NULL) {
        return push_error(L, "Process not found!");
    }
    luaL_getmetatable(L, PROCESS_METATABLE);
    lua_setmetatable(L, -2);

    memset(p->stdio, 0, sizeof(p->stdio)); // zero out stdio

    // if second argument is a table, check options for - assume process group
    if (lua_type(L, 2) == LUA_TTABLE) {                     // pid options process
        lua_getfield(L, 2, "isSeparateProcessGroup");       // pid options process isSeparateProcessGroup
        if (lua_isboolean(L, -1) && lua_toboolean(L, -1)) { // pid options process isSeparateProcessGroup
// we do not have job, group will be controlled on per process basis...
#ifdef _WIN32
            new_process_group(L, (HANDLE)NULL); // pid options process isSeparateProcessGroup process-group
#else
            new_process_group(L, (pid_t)pid); // pid options process isSeparateProcessGroup process-group
#endif
            lua_getiuservalue(L, -1, 1); // pid options process isSeparateProcessGroup process-group process-table
            lua_rotate(L, -2, 1);        // pid options process isSeparateProcessGroup process-table process-group
            lua_setiuservalue(L, -4, 1); // pid options process isSeparateProcessGroup process-table

            lua_pushvalue(L, -3);  // pid options process isSeparateProcessGroup process-table process
            lua_rawseti(L, -2, 1); // pid options process isSeparateProcessGroup process-table
            lua_pop(L, 1);         // pid options process isSeparateProcessGroup
        }
        lua_pop(L, 1); // pid options process
    }
    p->status = -1;
    p->signal = 0;
#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        CloseHandle(hProcess);
        return push_error(L, "failed to open process");
    }
    p->isChild = 0;
    p->hProcess = hProcess;
    p->pid = (DWORD)pid;
#else
    if (kill(pid, 0) == -1) {
        return push_error(L, "failed to open process");
    }
    p->pid = pid;
#endif

    return 1;
}

static const struct luaL_Reg eliProcExtra[] = {
    {"spawn", eli_spawn},
    {"get_by_pid", eli_get_process_by_id},
    {NULL, NULL},
};

int
luaopen_eli_proc_extra(lua_State* L) {
    process_create_meta(L);
    process_group_create_meta(L);

    lua_newtable(L);
    luaL_setfuncs(L, eliProcExtra, 0);

    return 1;
}
