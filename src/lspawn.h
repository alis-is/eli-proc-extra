#include <stdio.h>
#include "lua.h"
#include "stdioChannel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "environ.h"

#include <unistd.h>
#include <spawn.h>
#endif

struct process;
typedef struct spawn_params
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
    stdioChannel* stdio[3];
} spawn_params;

int proc_create_meta(lua_State *L);

struct spawn_params *spawn_param_init(lua_State *L);
void spawn_param_filename(struct spawn_params *p, const char *filename);
void spawn_param_args(struct spawn_params *p);
void spawn_param_env(struct spawn_params *p);
#ifdef _WIN32
void spawn_param_redirect(struct spawn_params *p, int d, HANDLE h);
#else
void spawn_param_redirect(spawn_params *p, int d, int fd);
#endif
int spawn_param_execute(struct spawn_params *p);

int close_stdio_channel(stdioChannel* channel);
