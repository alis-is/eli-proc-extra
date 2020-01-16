#include "lua.h"
#include "lauxlib.h"
#include "lutil.h"
#include "lprocess.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include "environ.h"

#include <unistd.h>
#include <spawn.h>
#endif

struct spawn_params
{
    lua_State *L;
#ifdef _WIN32
    const char *cmdline;
    const char *environment;
    STARTUPINFO si;
#else
    const char *command, **argv, **envp;
    posix_spawn_file_actions_t redirect;
    posix_spawnattr_t attr;
#endif
};

#ifdef _WIN32
/* quotes and adds argument string to b */
static int add_argument(luaL_Buffer *b, const char *s)
{
    int oddbs = 0;
    luaL_addchar(b, '"');
    for (; *s; s++)
    {
        switch (*s)
        {
        case '\\':
            luaL_addchar(b, '\\');
            oddbs = !oddbs;
            break;
        case '"':
            luaL_addchar(b, '\\');
            oddbs = 0;
            break;
        default:
            oddbs = 0;
            break;
        }
        luaL_addchar(b, *s);
    }
    luaL_addchar(b, '"');
    return oddbs;
}
#endif

struct spawn_params *spawn_param_init(lua_State *L)
{
    struct spawn_params *p = lua_newuserdata(L, sizeof *p);
#ifdef _WIN32
    static const STARTUPINFO si = {sizeof si};
    p->L = L;
    p->cmdline = p->environment = 0;
    p->si = si;
#else
    p->L = L;
    p->command = 0;
    p->argv = p->envp = 0;
    posix_spawn_file_actions_init(&p->redirect);
    posix_spawnattr_init(&p->attr);
#endif
    return p;
}

void spawn_param_filename(struct spawn_params *p, const char *filename)
{
#ifdef _WIN32
    lua_State *L = p->L;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    if (add_argument(&b, lua_tostring(L, 1)))
    {
        luaL_error(L, "argument ends in odd number of backslashes");
        return;
    }
    luaL_pushresult(&b);
    lua_replace(L, 1);
    p->cmdline = lua_tostring(L, 1);
#else
    p->command = filename;
#endif
}

/* Converts a Lua array of strings to a null-terminated array of char pointers.
 * Pops a (0-based) Lua array and replaces it with a userdatum which is the
 * null-terminated C array of char pointers.  The elements of this array point
 * to the strings in the Lua array.  These strings should be associated with
 * this userdatum via a weak table for GC purposes, but they are not here.
 * Therefore, any function which calls this must make sure that these strings
 * remain available until the userdatum is thrown away.
 */
/* ... array -- ... vector */
static const char **get_argv(lua_State *L)
{
    size_t i;
    size_t n = lua_rawlen(L, -1);

    const char **argv = lua_newuserdata(L, (n + 2) * sizeof *argv);

    for (i = 1; i <= n; i++)
    {
        lua_rawgeti(L, -2, i); /* ... argt argv arg */
        argv[i] = lua_tostring(L, -1);
        if (!argv[i])
        {
            luaL_error(L, "expected string for argument %d, got %s",
                       i, lua_typename(L, lua_type(L, -1)));
            return 0;
        }
        lua_pop(L, 1); /* ... argt argv */
    }
    argv[n + 1] = 0;
    lua_pop(L, 2);
    //lua_replace(L, -2); /* ... argv */

    return argv;
}

#ifdef _WIN32
static char *to_win_argv(lua_State *L, const char **argv)
{
    luaL_Buffer b;
    //lua_pop(L, 1); // pop argv
    luaL_buffinit(L, &b);
    for (; *argv; argv++)
    {

        const char *s = *argv;
        add_argument(&b, s);

        if (*(argv + 1) != 0)
        {
            luaL_addchar(&b, ' ');
        }
    }
    luaL_pushresult(&b);
    char *cmdline = luaL_tolstring(L, -1, b.size);
    lua_replace(L, 1);
    return cmdline;
}
#endif

/* ... argtab -- ... argtab vector */
void spawn_param_args(struct spawn_params *p)
{
    lua_State *L = p->L;
    const char **argv = get_argv(L); // cmd opts argv
#ifdef _WIN32
    argv[0] = lua_tostring(L, 1);
    //build_cmdline(L, argv); // cmdline opts
    p->cmdline = to_win_argv(L, argv);
#else
    argv[0] = p->command;
    p->argv = argv;
#endif
}

/* ... envtab -- ... envtab vector */
static const char **get_env(lua_State *L)
{
    size_t i = 0;
    lua_pushnil(L); /* ... envtab nil */
    size_t n = 0;
    for (i = 1; lua_next(L, -2); i++)
    {                  // envtab k v
        lua_pop(L, 1); // envtab k
        n++;
    } // envtab

    const char **env = lua_newuserdata(L, (n + 1) * sizeof *env); // envtab nil env
    lua_pushnil(L);                                               // envtab env nil

    for (i = 0; lua_next(L, -3); i++)
    { /* ... envtab env k v */
        size_t klen, vlen;
        const char *k = lua_tolstring(L, -2, &klen);
        if (!k)
        {
            luaL_error(L, "expected string for environment variable name, got %s",
                       lua_typename(L, lua_type(L, -2)));
            return NULL;
        }
        const char *v = lua_tolstring(L, -1, &vlen);
        if (!v)
        {
            luaL_error(L, "expected string for environment variable value, got %s",
                       lua_typename(L, lua_type(L, -1)));
            return NULL;
        }
        lua_pop(L, 1); // envtab env k

        char *t =  malloc((klen + vlen + 2) * sizeof(char));
        memcpy(t, k, klen);
        t[klen] = '=';
        memcpy(t + klen + 1, v, vlen + 1);
        env[i] = t;
    } /* ... envtab env */
    env[n] = 0;
    // ua_replace(L, -2); /* ... env */
    lua_pop(L, 2);
    return env;
}

#ifdef _WIN32
static char *to_win_env(lua_State *L, const char **env)
{
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (; *env; env++)
    {
        const char *s = *env;
        luaL_addlstring(&b, s, strlen(s) + 1); // add string including '\0'
    }
    luaL_addchar(&b, '\0'); // final '\0'
    luaL_pushresult(&b);

    char *winEnv = luaL_tolstring(L, -1, b.size);
    lua_pop(L, 1); // cleanup
    return winEnv;
}
#endif

void spawn_param_env(struct spawn_params *p)
{
    lua_State *L = p->L;
    const char **env = get_env(L);
#ifdef _WIN32
    p->environment = to_win_env(L, env);
#else
    p->envp = env;
#endif
}

#ifdef _WIN32
void spawn_param_redirect(struct spawn_params *p, const char *stdname, HANDLE h)
{
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (!(p->si.dwFlags & STARTF_USESTDHANDLES))
    {
        p->si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        p->si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        p->si.dwFlags |= STARTF_USESTDHANDLES;
    }
    switch (stdname[3])
    {
    case 'i':
        p->si.hStdInput = h;
        break;
    case 'o':
        p->si.hStdOutput = h;
        break;
    case 'e':
        p->si.hStdError = h;
        break;
    }
}
#else
void spawn_param_redirect(struct spawn_params *p, const char *stdname, int fd)
{
    int d;
    switch (stdname[3])
    {
    case 'i':
        d = STDIN_FILENO;
        break;
    case 'o':
        d = STDOUT_FILENO;
        break;
    case 'e':
        d = STDERR_FILENO;
        break;
    }
    posix_spawn_file_actions_adddup2(&p->redirect, fd, d);
}
#endif

int spawn_param_execute(struct spawn_params *p)
{
    lua_State *L = p->L;
    int ret;
    struct process *proc;
#ifdef _WIN32
    char *c, *e;
    PROCESS_INFORMATION pi;
#else
    if (!p->argv)
    {
        p->argv = lua_newuserdata(L, 2 * sizeof *p->argv);
        p->argv[0] = p->command;
        p->argv[1] = 0;
    }
    if (!p->envp)
        p->envp = (const char **)environ;
#endif
    proc = lua_newuserdata(L, sizeof *proc);
    luaL_getmetatable(L, PROCESS_METATABLE);
    lua_setmetatable(L, -2);
    proc->status = -1;

#ifdef _WIN32
    c = strdup(p->cmdline);
    e = (char *)p->environment; /* strdup(p->environment); */
    ret = CreateProcess(0, c, 0, 0, TRUE, 0, e, 0, &p->si, &pi);
    free(c);

    if (!ret)
        return windows_pusherror(L, GetLastError(), -2);
    proc->hProcess = pi.hProcess;
    proc->dwProcessId = pi.dwProcessId;
    return 1;
#else
    errno = 0;

    ret = posix_spawnp(&proc->pid, p->command, &p->redirect, &p->attr,
                       (char *const *)p->argv, (char *const *)p->envp);
    posix_spawn_file_actions_destroy(&p->redirect);
    posix_spawnattr_destroy(&p->attr);
    return ret != 0 /*|| errno != 0*/ ? push_error(L, NULL) : 1;
#endif
}
