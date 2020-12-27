#include "lua.h"
#include "lauxlib.h"

#include "lspawn.h"
#include "lutil.h"
#include "lprocess.h"

#include <string.h>

#ifdef _WIN32
#include <windows.h>

#define _lsleep Sleep
#define SLEEP_MULTIPLIER 1e3
#else
#include <unistd.h>

#define _lsleep usleep
#define SLEEP_MULTIPLIER 1e6
#endif

static void get_redirect(lua_State *L, const char *stdname, int idx, struct spawn_params *p) {
    stdioChannel* channel = malloc(sizeof(stdioChannel));
    lua_getfield(L, idx, stdname);
    int stdioKind;

    switch (stdname[3])
    {
        case 'i':
            stdioKind = STDIO_STDIN;
            break;
        case 'o':
            stdioKind = STDIO_STDOUT;
            break;
        case 'e':
            stdioKind = STDIO_STDERR;
            break;
    }

    int top = lua_gettop(L);
    switch (lua_type(L, -1))
    {
        case LUA_TNIL: // default pipe
            channel->kind = STDIO_CHANNEL_OWN_PIPE_KIND;
            break;
        case LUA_TSTRING:
            const char * kind = luaL_optstring(L, -1, "ignore"); // fallback to default pipe mode
            if (strcmp(kind, "ignore") == 0) {
                channel->kind = STDIO_CHANNEL_IGNORE_KIND;
            } else if (strcmp(kind, "inherit") == 0) {
                channel->kind = STDIO_CHANNEL_INHERIT_KIND;
                #ifdef _WIN32
                    spawn_param_redirect(p, stdioKind, GetStdHandle(-10 + (-1 * stdioKind)));
                #else
                    spawn_param_redirect(p, stdioKind, stdioKind);
                #endif
            } else if (strcmp(kind, "pipe") == 0) {
                channel->kind = STDIO_CHANNEL_OWN_PIPE_KIND;
                PIPE_DESCRIPTORS descriptors;
                if (new_pipe(&descriptors) == -1) {
                    luaL_error(L, "Failed to create pipe!");
                    return;
                };
                /**
                 * we always pass second file descriptor to child process
                 * and use first one as ELI_PIPE_END
                 * */
                ELI_PIPE_END* pipeEnd = malloc(sizeof(ELI_PIPE_END));
                #ifdef _WIN32
                    pipeEnd->h = descriptors.ph[0];
                #else
                    pipeEnd->fd = descriptors.fd[0];
                #endif
                pipeEnd->mode = stdioKind == STDIO_STDIN ? "w" : "r";
                channel->pipeEnd = pipeEnd;
                #ifdef _WIN32
                    spawn_param_redirect(p, stdioKind, descriptors.ph[1]);
                    CloseHandle(descriptors.ph[1]);
                #else
                    spawn_param_redirect(p, stdioKind, descriptors.fd[1]);
                    close(descriptors.fd[1]);
                #endif
            } else {
                luaL_error(L, "Invalid stdio type: %s!");
                return;
            }
            break;      
        case LUA_TUSERDATA:
            lua_getmetatable(L, idx);
            luaL_getmetatable(L, LUA_FILEHANDLE);
            luaL_getmetatable(L, PIPE_METATABLE);
            if (lua_rawequal(L, -2, -3))
            { // file
                lua_pop(L, lua_gettop(L) - top);
                luaL_Stream *fh = (luaL_Stream *)luaL_checkudata(L, -1, "FILE*");
                if (fh->closef == 0 || fh->f == NULL)
                {
                    luaL_error(L, "%s: closed file");
                    return;
                }
                
                channel->kind = STDIO_CHANNEL_FILE_KIND;
                channel->file = fh;
                #ifdef _WIN32
                    spawn_param_redirect(p, stdioKind, (HANDLE)_get_osfhandle(_fileno(fh)));
                #else
                    spawn_param_redirect(p, stdioKind, fileno(fh->f));

                #endif
            }
            if (lua_rawequal(L, -1, -3))
            { // eli pipe
                lua_pop(L, lua_gettop(L) - top);
                ELI_PIPE_END *_pipe = (ELI_PIPE_END *)luaL_checkudata(L, -1, "ELI_PIPE_END");
                if (_pipe->closed)
                {
                    luaL_error(L, "%s: closed pipe");
                    return;
                }
                
                channel->kind = STDIO_CHANNEL_PIPE_END_KIND;
                channel->pipeEnd = _pipe;
                #ifdef _WIN32
                    spawn_param_redirect(p, stdioKind, _pipe->h);
                #else
                    spawn_param_redirect(p, stdioKind, _pipe->fd);
                #endif
            }
            lua_pop(L, lua_gettop(L) - top);
            luaL_typeerror(L, -1, "FILE*/ELI_PIPE_END");
            break;
    }

    p->stdio[stdioKind] = channel;
    lua_pop(L, 1);
    return;
}

static void get_redirects(lua_State *L, int idx, struct spawn_params *p) {
    lua_getfield(L, idx, "stdio");
    // pipe, inherit, ignore are supported values
    switch (lua_type(L, -1))
    {
        default:
            return luaL_error(L, "bad args option (table expected, got %s)",
                                luaL_typename(L, -1));
        case LUA_TNIL:
            // fall through
        case LUA_TSTRING: 
            const char* stdio = luaL_optstring(L, -1, "pipe");
            stdout = stdio;
            stderr = stdio;
            stdin = stdio;
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
        case LUA_TTABLE:
            break;
    }
    get_redirect(L, "stdin", -1, p);
    get_redirect(L, "stdout", -1, p);
    get_redirect(L, "stderr", -1, p);

    lua_pop(L, 1);
}

/* filename [args-opts] -- proc/nil error */
/* args-opts -- proc/nil error */
static int eli_spawn(lua_State *L)
{
    struct spawn_params *params;
    int have_options;
    switch (lua_type(L, 1))
    {
    default:
        return luaL_typeerror(L, 1, "string or table");
    case LUA_TSTRING:
        switch (lua_type(L, 2))
        {
        default:
            return luaL_typeerror(L, 2, "table");
        case LUA_TNONE:
            have_options = 0;
            break;
        case LUA_TTABLE:
            have_options = 1;
            break;
        }
        break;
    case LUA_TTABLE:
        have_options = 1;
        lua_getfield(L, 1, "command"); /* opts ... cmd */
        if (!lua_isnil(L, -1))
        {
            /* convert {command=command,arg1,...} to command {arg1,...} */
            lua_insert(L, 1); /* cmd opts ... */
        }
        else
        {
            /* convert {arg0,arg1,...} to arg0 {arg1,...} */
            size_t i, n = lua_rawlen(L, 1);
            lua_rawgeti(L, 1, 1); /* opts ... nil cmd */
            lua_insert(L, 1);     /* cmd opts ... nil */
            for (i = 2; i <= n; i++)
            {
                lua_rawgeti(L, 2, i);     /* cmd opts ... nil argi */
                lua_rawseti(L, 2, i - 1); /* cmd opts ... nil */
            }
            lua_rawseti(L, 2, n); /* cmd opts ... */
        }
        if (lua_type(L, 1) != LUA_TSTRING)
            return luaL_error(L, "bad command option (string expected, got %s)",
                              luaL_typename(L, 1));
        break;
    }

    params = spawn_param_init(L);
    /* get filename to execute */
    spawn_param_filename(params, lua_tostring(L, 1));
    /* get arguments, environment, and redirections */
    if (have_options)
    {
        lua_getfield(L, 2, "args"); /* cmd opts ... argtab */
        switch (lua_type(L, -1))
        {
        default:
            return luaL_error(L, "bad args option (table expected, got %s)",
                              luaL_typename(L, -1));
        case LUA_TNIL:
            lua_pop(L, 1);       /* cmd opts ... */
            lua_pushvalue(L, 2); /* cmd opts ... opts */
            /*FALLTHRU*/
        case LUA_TTABLE:
            if (lua_rawlen(L, 2) > 0)
                return luaL_error(L, "cannot specify both the args option and array values");
            spawn_param_args(params); /* cmd opts ... */
            break;
        }
        lua_getfield(L, 2, "env"); /* cmd opts ... envtab */
        switch (lua_type(L, -1))
        {
        default:
            return luaL_error(L, "bad env option (table expected, got %s)",
                              luaL_typename(L, -1));
        case LUA_TNIL:
            break;
        case LUA_TTABLE:
            spawn_param_env(params); /* cmd opts ... */
            break;
        }
        get_redirects(L, 2, params);  /* cmd opts ... */
    }
    return spawn_param_execute(params); /* proc/nil error */
}

static const struct luaL_Reg eliProcExtra[] = {
    {"spawn", eli_spawn},
    {NULL, NULL},
};

int luaopen_eli_proc_extra(lua_State *L)
{
    process_create_meta(L);

    lua_newtable(L);
    luaL_setfuncs(L, eliProcExtra, 0);
    return 1;
}
