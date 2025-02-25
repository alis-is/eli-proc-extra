#include "lauxlib.h"
#include "lerror.h"
#include "lprocess.h"
#include "lprocess_group.h"
#include "lua.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "lspawn.h"

#ifdef _WIN32
/* quotes and adds argument string to b */
static void
add_argument(luaL_Buffer* b, const char* s) {
    const char* tmps = s;
    int hasSpace = 0;
    for (; *tmps; tmps++) {
        if (isspace(*tmps)) {
            hasSpace = 1;
            break;
        }
    }
    if (!hasSpace) {
        luaL_addstring(b, s);
        return;
    }
    luaL_addchar(b, '"');
    for (; *s; s++) {
        switch (*s) {
            case '\\': luaL_addchar(b, '\\'); break;
            case '"': luaL_addchar(b, '\\'); break;
            default: break;
        }
        luaL_addchar(b, *s);
    }
    luaL_addchar(b, '"');
}

#define close _close
#endif

spawn_params*
spawn_param_init(lua_State* L) {
    spawn_params* p = lua_newuserdatauv(L, sizeof *p, 0);
    memset(p, 0, sizeof *p);
#ifdef _WIN32
    static const STARTUPINFO si = {sizeof si};
    p->cmdline = p->environment = 0;
    p->si = si;
#else
    p->command = 0;
    p->argv = p->envp = 0;
    p->redirect[0] = p->redirect[1] = p->redirect[2] = -1;
#endif
    p->username = NULL;
    p->password = NULL;
    p->stdio[STDIO_STDIN] = NULL;
    p->stdio[STDIO_STDOUT] = NULL;
    p->stdio[STDIO_STDERR] = NULL;
    return p;
}

void
spawn_param_filename(spawn_params* p, const char* filename) {
#ifdef _WIN32
    p->cmdline = filename;
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
static const char**
get_argv(lua_State* L) {
    size_t i;
    size_t n = lua_rawlen(L, -1);

    const char** argv = lua_newuserdatauv(L, (n + 2) * sizeof *argv, 0); // argt argv

    for (i = 1; i <= n; i++) {
        lua_rawgeti(L, -2, i); /* ... argt argv arg */
        argv[i] = lua_tostring(L, -1);
        if (!argv[i]) {
            luaL_error(L, "expected string for argument %d, got %s", i, lua_typename(L, lua_type(L, -1)));
            return 0;
        }
        lua_pop(L, 1); /* ... argt argv */
    }
    argv[n + 1] = 0;
    lua_pop(L, 1); /* ... argt */

    return argv;
}

#ifdef _WIN32
static const char*
to_win_argv(lua_State* L, const char** argv) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (; *argv; argv++) {

        const char* s = *argv;
        add_argument(&b, s);

        if (*(argv + 1) != 0) {
            luaL_addchar(&b, ' ');
        }
    }
    luaL_pushresult(&b);
    const char* cmdline = luaL_tolstring(L, -1, NULL);
    lua_pop(L, 2); // luaL_tolstring pushes a copy of the string so we have to pop twice
    return cmdline;
}
#endif

/* ... argtab -- ... argtab vector */
void
spawn_param_args(lua_State* L, spawn_params* p) {
    const char** argv = get_argv(L); // cmd opts argv
#ifdef _WIN32
    argv[0] = lua_tostring(L, 1);
    // build_cmdline(L, argv); // cmdline opts
    p->cmdline = to_win_argv(L, argv);
#else
    argv[0] = p->command;
    p->argv = argv;
#endif
}

/* ... envtab -- ... envtab vector */
static const char**
get_env(lua_State* L) {
    size_t i = 0;
    lua_pushnil(L); /* ... envtab nil */
    size_t n = 0;
    for (i = 1; lua_next(L, -2); i++) { // envtab k v
        lua_pop(L, 1);                  // envtab k
        n++;
    } // envtab

    const char** env = lua_newuserdatauv(L, (n + 1) * sizeof *env, 0); // envtab env
    lua_pushnil(L);                                                    // envtab env nil

    for (i = 0; lua_next(L, -3); i++) { /* ... envtab env k v */
        size_t klen, vlen;
        const char* k = lua_tolstring(L, -2, &klen);
        if (!k) {
            luaL_error(L, "expected string for environment variable name, got %s", lua_typename(L, lua_type(L, -2)));
            return NULL;
        }
        const char* v = lua_tolstring(L, -1, &vlen);
        if (!v) {
            luaL_error(L, "expected string for environment variable value, got %s", lua_typename(L, lua_type(L, -1)));
            return NULL;
        }
        lua_pop(L, 1); // envtab env k

        char* t = malloc((klen + vlen + 2) * sizeof(char));
        memcpy(t, k, klen);
        t[klen] = '=';
        memcpy(t + klen + 1, v, vlen + 1);
        env[i] = t;
    } /* ... envtab env */
    env[n] = 0;
    lua_pop(L, 1); // ... envtab
    return env;
}

#ifdef _WIN32
static const char*
to_win_env(lua_State* L, const char** env) {
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    for (; *env; env++) {
        const char* s = *env;
        luaL_addlstring(&b, s, strlen(s) + 1); // add string including '\0'
    }
    luaL_addchar(&b, '\0'); // final '\0'
    luaL_pushresult(&b);

    const char* winEnv = luaL_tolstring(L, -1, NULL);
    lua_pop(L, 2); // luaL_tolstring pushes a copy of the string so we have to pop twice
    return winEnv;
}
#endif

void
spawn_param_env(lua_State* L, spawn_params* p) {
    const char** env = get_env(L);
#ifdef _WIN32
    p->environment = to_win_env(L, env);
#else
    p->envp = env;
#endif
}

#ifdef _WIN32
void
spawn_param_redirect(spawn_params* p, int d, HANDLE h) {
    SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    if (!(p->si.dwFlags & STARTF_USESTDHANDLES)) {
        p->si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        p->si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        p->si.dwFlags |= STARTF_USESTDHANDLES;
    }
    switch (d) {
        case STDIO_STDIN: p->si.hStdInput = h; break;
        case STDIO_OUTPUT_STREAMS:
            p->si.hStdOutput = h;
            p->si.hStdError = h;
            break;
        case STDIO_STDOUT: p->si.hStdOutput = h; break;
        case STDIO_STDERR: p->si.hStdError = h; break;
    }
}

void
spawn_param_redirect_inherit(spawn_params* p, int d) {
    switch (d) {
        case STDIO_STDIN: spawn_param_redirect(p, d, GetStdHandle(STD_INPUT_HANDLE)); break;
        case STDIO_STDOUT: spawn_param_redirect(p, d, GetStdHandle(STD_OUTPUT_HANDLE)); break;
        case STDIO_STDERR: spawn_param_redirect(p, d, GetStdHandle(STD_ERROR_HANDLE)); break;
        case STDIO_OUTPUT_STREAMS:
            spawn_param_redirect(p, STDIO_STDOUT, GetStdHandle(STD_OUTPUT_HANDLE));
            spawn_param_redirect(p, STDIO_STDERR, GetStdHandle(STD_ERROR_HANDLE));
            break;
        default: break;
    }
}

#else
void
spawn_param_redirect(spawn_params* p, int d, int fd) {
    switch (d) {
        case STDIO_OUTPUT_STREAMS:
            p->redirect[STDIO_STDOUT] = fd;
            p->redirect[STDIO_STDERR] = fd;
            break;
        default: p->redirect[d] = fd; break;
    }
}

void
spawn_param_redirect_inherit(spawn_params* p, int d) {
    switch (d) {
        case STDIO_STDIN: spawn_param_redirect(p, d, STDIN_FILENO); break;
        case STDIO_STDOUT: spawn_param_redirect(p, d, STDOUT_FILENO); break;
        case STDIO_STDERR: spawn_param_redirect(p, d, STDERR_FILENO); break;
        case STDIO_OUTPUT_STREAMS:
            spawn_param_redirect(p, STDIO_STDOUT, STDOUT_FILENO);
            spawn_param_redirect(p, STDIO_STDERR, STDERR_FILENO);
            break;

        default: break;
    }
}
#endif

void
close_proc_stdio_channel(process* p, int stdKind) {
    stdio_channel* channel = p->stdio[stdKind];
    close_stdio_channel(channel);

    for (int i = 0; i < 3; i++) {
        if (p->stdio[i] == channel) { // if we have a reference to the channel
            p->stdio[i] = NULL;
        }
    }
}

#ifndef _WIN32

static void
child_finalize_error(int error_pipe) {
    int err = errno;
    write(error_pipe, &err, sizeof(err));
    close(error_pipe);
    _exit(EXIT_FAILURE);
}

static int
child_init(int error_pipe, int uid, int gid, pid_t pgid, spawn_params* p) {
    int flags = fcntl(error_pipe, F_GETFD);
    if (flags == -1) {
        child_finalize_error(error_pipe);
    }

    flags |= FD_CLOEXEC;
    if (fcntl(error_pipe, F_SETFD, flags) == -1) {
        child_finalize_error(error_pipe);
    }

    if (p->username != NULL && gid != -1 && initgroups(p->username, gid) != 0) {
        child_finalize_error(error_pipe);
    }

    if ((uid != -1 && setgid(uid) != 0) || (gid != -1 && setuid(gid) != 0)) {
        child_finalize_error(error_pipe);
    }

    if (pgid != -1 && setpgid(0, pgid) != 0) {
        child_finalize_error(error_pipe);
    }

    for (int i = 0; i < 3; i++) {
        if (p->redirect[i] != -1) {
            dup2(p->redirect[i], i);
        }
    }

    execve_spawnp(p->command, (char* const*)p->argv, (char* const*)p->envp);
    child_finalize_error(error_pipe);
    return 0;
}

#endif

int
spawn_param_execute(lua_State* L) {
    spawn_params* p = (spawn_params*)lua_touserdata(L, 1);

    int success = 1;
#ifdef _WIN32
    char *c, *e;
    PROCESS_INFORMATION pi;
#else
    if (!p->argv) {
        p->argv = lua_newuserdatauv(L, 2 * sizeof *p->argv, 0);
        p->argv[0] = p->command;
        p->argv[1] = 0;
        lua_pop(L, 1); // pop argv
    }
    if (p->envp == 0) {
        p->envp = (const char**)environ;
    }
#endif
    process* proc = lua_newuserdatauv(L, sizeof *proc, 1); // params process_group proc
    luaL_getmetatable(L, PROCESS_METATABLE);
    lua_setmetatable(L, -2);
    proc->status = -1;
    proc->signal = 0;
    proc->stdio[STDIO_STDIN] = p->stdio[STDIO_STDIN];
    proc->stdio[STDIO_STDOUT] = p->stdio[STDIO_STDOUT];
    proc->stdio[STDIO_STDERR] = p->stdio[STDIO_STDERR];
#ifdef _WIN32
    c = strdup(p->cmdline);
    e = (char*)p->environment; /* strdup(p->environment); */
    proc->isChild = 1;         // if we decide to use different creation flags we may have to adjust
    DWORD creationFlags =
        CREATE_NEW_PROCESS_GROUP
        | (p->create_process_group || luaL_testudata(L, 2, PROCESS_GROUP_METATABLE) != NULL ? CREATE_NEW_CONSOLE : 0);
    success = CreateProcess(0, c, 0, 0, TRUE, creationFlags, e, 0, &p->si, &pi) != 0;
    free(c);

    if (success == 1) {
        proc->hProcess = pi.hProcess;
        proc->pid = pi.dwProcessId;
        if (p->create_process_group) {
            HANDLE process_group = CreateJobObject(NULL, NULL);
            if (process_group == NULL) {
                success = 0;
            } else {
                // create process group
                new_process_group(L, process_group); // params process_group proc process_group
                lua_replace(L, 2);                   // params process_group proc
            }
        }

        lua_rotate(L, -2, -1); // params proc process_group/nil
        process_group* pg = (process_group*)luaL_testudata(L, -1, PROCESS_GROUP_METATABLE); // params proc process_group
        if (pg != NULL && success == 1) {
            if (!AssignProcessToJobObject(pg->gid, proc->hProcess)) {
                success = 0;
            } else {
                // params proc process_group
                lua_getiuservalue(L, -1, 1); // params proc process_group process_table
                lua_rotate(L, -2, 1);        // params proc process_table process_group
                lua_setiuservalue(L, -3, 1); // params proc process_table
                // append process to process group
                // params proc process_table
                lua_pushvalue(L, -2);                      // params proc process_table process
                lua_rawseti(L, -2, lua_rawlen(L, -2) + 1); // params proc process_table
                lua_pop(L, 1);                             // params proc
            }
        } else {
            // keep just params proc
            lua_pop(L, 1); // params proc
        }
    }

#else
    errno = 0;
    // impersonation
    int uid = -1, gid = -1;
    if (success == 1 && p->username != NULL) {
        struct passwd* pwd = getpwnam(p->username);
        if (pwd == NULL) {
            success = 0;
        } else {
            uid = pwd->pw_uid;
            gid = pwd->pw_gid;
        }
    }

    // process group
    // params process_group proc
    pid_t pid, pgid = -1;
    if (p->create_process_group) {
        pgid = 0; // create new process group
    } else {
        process_group* pg = (process_group*)luaL_testudata(L, 2, PROCESS_GROUP_METATABLE);
        if (pg != NULL) {
            pgid = pg->gid;
        }
        lua_pushvalue(L, 2);         // params process_group proc process_group
        lua_setiuservalue(L, -2, 1); // params process_group proc
    }
    int pipefd[2];
    if (success == 1 && pipe(pipefd) == -1) {
        success = 0;
    }

    if (success == 1) {
        pid = fork();
        if (pid == -1) {
            close(pipefd[0]);
            close(pipefd[1]);
            success = 0;
        }
    }

    if (success == 1) {
        if (pid == 0) {
            // child
            close(pipefd[0]); // Close read end of the pipe

            child_init(pipefd[1], uid, gid, pgid, p);
        } else {
            // parent
            close(pipefd[1]); // Close write end of the pipe
            int err;
            if (read(pipefd[0], &err, sizeof(err)) > 0) {
                waitpid(pid, NULL, 0); // Clean up the child process
                errno = err;
                success = 0;
            }

            close(pipefd[0]);
        }
    }

    if (success == 1) {
        proc->pid = pid;

        if (p->create_process_group) {
            new_process_group(L, proc->pid); // params process_group proc process_group
            lua_copy(L, -1, -3);             // params process_group proc process_group
            lua_setiuservalue(L, -2, 1);     // params process_group proc
        }
        // inject process into process group
        process_group* pg = (process_group*)luaL_testudata(L, 2, PROCESS_GROUP_METATABLE);
        if (pg != NULL) {
            lua_getiuservalue(L, 2, 1);                // params process_group proc process_table
            lua_pushvalue(L, -2);                      // params process_group process process_table process
            lua_rawseti(L, -2, lua_rawlen(L, -2) + 1); // params process_group process process_table
            lua_pop(L, 1);                             // params process_group process
        }
    }

#endif
    close_stdio_channel_to_close(p->stdio[STDIO_STDIN]);
    close_stdio_channel_to_close(p->stdio[STDIO_STDOUT]);
    close_stdio_channel_to_close(p->stdio[STDIO_STDERR]);

    if (success != 1) {
        close_proc_stdio_channel(proc, STDIO_STDIN);
        close_proc_stdio_channel(proc, STDIO_STDOUT);
        close_proc_stdio_channel(proc, STDIO_STDERR);
        return push_error(L, NULL);
    }
    return 1;
}
