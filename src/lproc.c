#include "lua.h"
#include "lauxlib.h"

#include "lspawn.h"
#include "lcwd.h"
#include "lutil.h"
#include "lprocess.h"
#include "lpipe.h"

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

/* seconds --
 * interval units -- */
static int eli_sleep(lua_State *L)
{
    lua_Number interval = luaL_checknumber(L, 1);
    lua_Number units = luaL_optnumber(L, 2, 1);
    _lsleep(SLEEP_MULTIPLIER * interval / units);
    return 0;
}

static void get_redirect(lua_State *L,
                         int idx, const char *stdname, struct spawn_params *p)
{
    lua_getfield(L, idx, stdname);
    int top = lua_gettop(L);
    if (!lua_isnil(L, -1))
    {
        lua_getmetatable(L, -1);
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
            spawn_param_redirect(p, stdname, fileno(fh->f));
            lua_pop(L, 1);
            return;
        }
        if (lua_rawequal(L, -1, -3))
        { // eli pipe
            lua_pop(L, lua_gettop(L) - top);
            ELI_PIPE *_pipe = (ELI_PIPE *)luaL_checkudata(L, -1, "ELI_PIPE");
            if (_pipe->closed)
            {
                luaL_error(L, "%s: closed pipe");
                return;
            }
            spawn_param_redirect(p, stdname, _pipe->fd);
            lua_pop(L, 1);
            return;
        }
        lua_pop(L, lua_gettop(L) - top);
        luaL_typeerror(L, -1, "FILE*/ELI_PIPE");
        return;
    }
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
            if (0)               /*FALLTHRU*/
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
        get_redirect(L, 2, "stdin", params);  /* cmd opts ... */
        get_redirect(L, 2, "stdout", params); /* cmd opts ... */
        get_redirect(L, 2, "stderr", params); /* cmd opts ... */
    }
    return spawn_param_execute(params); /* proc/nil error */
}

static const struct luaL_Reg eliProcExtra[] = {
    {"sleep", eli_sleep},
    {"spawn", eli_spawn},
    {"chdir", eli_chdir},
    {"cwd", eli_cwd},
    {NULL, NULL},
};

int luaopen_eli_proc_extra(lua_State *L)
{
    process_create_meta(L);

    lua_newtable(L);
    luaL_setfuncs(L, eliProcExtra, 0);
    return 1;
}
