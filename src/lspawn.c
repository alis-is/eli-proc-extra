#include "lauxlib.h"
#include "lprocess.h"
#include "lprocess_group.h"
#include "lua.h"
#include "lutil.h"

#include "lspawn.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
/* quotes and adds argument string to b */
static void add_argument(luaL_Buffer *b, const char *s) {
  const char *tmps = s;
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
    case '\\':
      luaL_addchar(b, '\\');
      break;
    case '"':
      luaL_addchar(b, '\\');
      break;
    default:
      break;
    }
    luaL_addchar(b, *s);
  }
  luaL_addchar(b, '"');
}

#define close _close
#endif

spawn_params *spawn_param_init(lua_State *L) {
  spawn_params *p = lua_newuserdatauv(L, sizeof *p, 0);
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
  p->stdio[STDIO_STDIN] = NULL;
  p->stdio[STDIO_STDOUT] = NULL;
  p->stdio[STDIO_STDERR] = NULL;
  return p;
}

void spawn_param_filename(spawn_params *p, const char *filename) {
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
static const char **get_argv(lua_State *L) {
  size_t i;
  size_t n = lua_rawlen(L, -1);

  const char **argv = lua_newuserdatauv(L, (n + 2) * sizeof *argv, 0);

  for (i = 1; i <= n; i++) {
    lua_rawgeti(L, -2, i); /* ... argt argv arg */
    argv[i] = lua_tostring(L, -1);
    if (!argv[i]) {
      luaL_error(L, "expected string for argument %d, got %s", i,
                 lua_typename(L, lua_type(L, -1)));
      return 0;
    }
    lua_pop(L, 1); /* ... argt argv */
  }
  argv[n + 1] = 0;
  lua_pop(L, 2);
  // lua_replace(L, -2); /* ... argv */

  return argv;
}

#ifdef _WIN32
static const char *to_win_argv(lua_State *L, const char **argv) {
  luaL_Buffer b;
  // lua_pop(L, 1); // pop argv
  luaL_buffinit(L, &b);
  for (; *argv; argv++) {

    const char *s = *argv;
    add_argument(&b, s);

    if (*(argv + 1) != 0) {
      luaL_addchar(&b, ' ');
    }
  }
  luaL_pushresult(&b);
  const char *cmdline = luaL_tolstring(L, -1, NULL);
  lua_replace(L, 1);
  return cmdline;
}
#endif

/* ... argtab -- ... argtab vector */
void spawn_param_args(spawn_params *p) {
  lua_State *L = p->L;
  const char **argv = get_argv(L); // cmd opts argv
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
static const char **get_env(lua_State *L) {
  size_t i = 0;
  lua_pushnil(L); /* ... envtab nil */
  size_t n = 0;
  for (i = 1; lua_next(L, -2); i++) { // envtab k v
    lua_pop(L, 1);                    // envtab k
    n++;
  } // envtab

  const char **env =
      lua_newuserdatauv(L, (n + 1) * sizeof *env, 0); // envtab nil env
  lua_pushnil(L);                                     // envtab env nil

  for (i = 0; lua_next(L, -3); i++) { /* ... envtab env k v */
    size_t klen, vlen;
    const char *k = lua_tolstring(L, -2, &klen);
    if (!k) {
      luaL_error(L, "expected string for environment variable name, got %s",
                 lua_typename(L, lua_type(L, -2)));
      return NULL;
    }
    const char *v = lua_tolstring(L, -1, &vlen);
    if (!v) {
      luaL_error(L, "expected string for environment variable value, got %s",
                 lua_typename(L, lua_type(L, -1)));
      return NULL;
    }
    lua_pop(L, 1); // envtab env k

    char *t = malloc((klen + vlen + 2) * sizeof(char));
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
static const char *to_win_env(lua_State *L, const char **env) {
  luaL_Buffer b;
  luaL_buffinit(L, &b);
  for (; *env; env++) {
    const char *s = *env;
    luaL_addlstring(&b, s, strlen(s) + 1); // add string including '\0'
  }
  luaL_addchar(&b, '\0'); // final '\0'
  luaL_pushresult(&b);

  const char *winEnv = luaL_tolstring(L, -1, NULL);
  lua_pop(L, 1); // cleanup
  return winEnv;
}
#endif

void spawn_param_env(spawn_params *p) {
  lua_State *L = p->L;
  const char **env = get_env(L);
#ifdef _WIN32
  p->environment = to_win_env(L, env);
#else
  p->envp = env;
#endif
}

#ifdef _WIN32
void spawn_param_redirect(spawn_params *p, int d, HANDLE h) {
  SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
  if (!(p->si.dwFlags & STARTF_USESTDHANDLES)) {
    p->si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    p->si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    p->si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    p->si.dwFlags |= STARTF_USESTDHANDLES;
  }
  switch (d) {
  case STDIO_STDIN:
    p->si.hStdInput = h;
    break;
  case STDIO_STDOUT:
    p->si.hStdOutput = h;
    break;
  case STDIO_STDERR:
    p->si.hStdError = h;
    break;
  }
}
#else
void spawn_param_redirect(spawn_params *p, int d, int fd) {
  posix_spawn_file_actions_adddup2(&p->redirect, fd, d);
}
#endif

static void close_toClose(process *p, int stdKind) {
  stdioChannel *channel = p->stdio[stdKind];
  if (channel == NULL)
    return;
  if (channel->fdToClose >= 0) {
    close(channel->fdToClose);
    channel->fdToClose = -1;
  }
}

void close_stdio_channel(process *p, int stdKind) {
  stdioChannel *channel = p->stdio[stdKind];
  if (channel == NULL)
    return;
  if (channel->kind == STDIO_CHANNEL_STREAM_KIND) {
    close(channel->stream->fd);
    free(channel->stream);
  }
  p->stdio[stdKind] = NULL;
  free(channel);
}

int spawn_param_execute(spawn_params *p) {
  lua_State *L = p->L;
  int success = 1;
  process *proc;
#ifdef _WIN32
  char *c, *e;
  PROCESS_INFORMATION pi;
#else
  if (!p->argv) {
    p->argv = lua_newuserdatauv(L, 2 * sizeof *p->argv, 0);
    p->argv[0] = p->command;
    p->argv[1] = 0;
  }
  if (p->envp == 0)
    p->envp = (const char **)environ;
#endif
  proc = lua_newuserdatauv(L, sizeof *proc, 0);
  luaL_getmetatable(L, PROCESS_METATABLE);
  lua_setmetatable(L, -2);
  proc->status = -1;
  proc->process_group_ref = 0;
  proc->stdio[STDIO_STDIN] = p->stdio[STDIO_STDIN];
  proc->stdio[STDIO_STDOUT] = p->stdio[STDIO_STDOUT];
  proc->stdio[STDIO_STDERR] = p->stdio[STDIO_STDERR];

#ifdef _WIN32
  c = strdup(p->cmdline);
  e = (char *)p->environment; /* strdup(p->environment); */
  proc->isSeparateProcessGroup =
      1; // if we decide to use different creation flags we may have to adjust
  success = CreateProcess(0, c, 0, 0, TRUE, CREATE_NEW_CONSOLE, e, 0, &p->si,
                          &pi) != 0;
  free(c);

  if (success == 1) {
    proc->hProcess = pi.hProcess;
    proc->dwProcessId = pi.dwProcessId;

    if (p->createProcessGroup) {
      HANDLE processGroup = CreateJobObject(NULL, NULL);
      if (processGroup == NULL) {
        success = 0;
      } else {
        // create process group
        new_process_group(L, processGroup);                    // proc group
        p->process_group_ref = luaL_ref(L, LUA_REGISTRYINDEX); // clean stack
      }
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, p->process_group_ref); // process_group
    process_group *pg =
        (process_group *)luaL_testudata(L, -1, PROCESS_GROUP_METATABLE);
    if (pg != NULL && success == 1) {
      if (!AssignProcessToJobObject(pg->hJob, proc->hProcess)) {
        success = 0;
      } else {
        proc->isGroupMember = !p->createProcessGroup;
        proc->isGroupLeader = p->createProcessGroup;
        proc->process_group_ref = p->process_group_ref;

        // append process to process group
        lua_rawgeti(L, LUA_REGISTRYINDEX,
                    pg->process_table_ref); // process_group process_table
        lua_pushvalue(L, -3); // process_group process_table process
        lua_rawseti(L, -2,
                    lua_rawlen(L, -2) + 1); // process_group process_table
        lua_pop(L, 1);                      // process_group
      }
    }
    lua_pop(L, 1); // clean stack
  }

#else
  errno = 0;
  if (p->createProcessGroup) {
    if (posix_spawnattr_setpgroup(&p->attr, 0) != 0) {
      success = 0;
    } else {
      proc->isGroupLeader = 1;
    }
  } else {
    lua_rawgeti(L, LUA_REGISTRYINDEX, p->process_group_ref); // process_group
    process_group *pg =
        (process_group *)luaL_testudata(L, -1, PROCESS_GROUP_METATABLE);
    if (pg != NULL) {
      if (posix_spawnattr_setpgroup(&p->attr, pg->gpid) != 0) {
        success = 0;
      } else {
        proc->isGroupMember = 1;
        proc->process_group_ref = p->process_group_ref;
      }
    }
    lua_pop(L, 1); // cleanup
  }

  if (success == 1) {
    success = posix_spawnp(&proc->pid, p->command, &p->redirect, &p->attr,
                           (char *const *)p->argv, (char *const *)p->envp) == 0;
    if (success == 1) {
      if (p->createProcessGroup) {
        new_process_group(L, proc->pid);
        proc->process_group_ref = luaL_ref(L, LUA_REGISTRYINDEX);
      }

      lua_rawgeti(L, LUA_REGISTRYINDEX,
                  proc->process_group_ref); // process_group
      process_group *pg =
          (process_group *)luaL_testudata(L, -1, PROCESS_GROUP_METATABLE);
      if (pg != NULL) {
        lua_rawgeti(L, LUA_REGISTRYINDEX,
                    pg->process_table_ref); // process_group process_table
        lua_pushvalue(L, -3); // process_group process_table process
        lua_rawseti(L, -2,
                    lua_rawlen(L, -2) + 1); // process_group process_table
        lua_pop(L, 1);                      // process_group
      }
      lua_pop(L, 1); // clean stack
    }
  }

  if (success == 1) {
    posix_spawn_file_actions_destroy(&p->redirect);
    posix_spawnattr_destroy(&p->attr);
  }
#endif
  close_toClose(proc, STDIO_STDIN);
  close_toClose(proc, STDIO_STDOUT);
  close_toClose(proc, STDIO_STDERR);

  if (success != 1) {
    close_stdio_channel(proc, STDIO_STDIN);
    close_stdio_channel(proc, STDIO_STDOUT);
    close_stdio_channel(proc, STDIO_STDERR);
#ifdef _WIN32
    return windows_pushlasterror(L);
#else
    return push_error(L, NULL);
#endif
  }
  return 1;
}
