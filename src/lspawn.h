#ifndef ELI_SPAWN_H_
#define ELI_SPAWN_H_

#include <stdio.h>
#include "lprocess.h"
#include "lprocess_group.h"
#include "lua.h"
#include "stdio_channel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "environ.h"

#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "execve_spawnp.h"

#endif

typedef struct spawn_params {
    lua_State* L;
#ifdef _WIN32
    const char* cmdline;
    const char* environment;
    STARTUPINFO si;
#else
    const char *command, **argv, **envp;
    // posix_spawn_file_actions_t redirect;
    // posix_spawnattr_t attr;
    int redirect[3];
#endif
    const char *username, *password;
    stdio_channel* stdio[3];
    int create_process_group;
} spawn_params;

int proc_create_meta(lua_State* L);

spawn_params* spawn_param_init(lua_State* L);
void spawn_param_filename(spawn_params* p, const char* filename);
void spawn_param_args(lua_State* L, spawn_params* p);
void spawn_param_env(lua_State* L, spawn_params* p);
#ifdef _WIN32
void spawn_param_redirect(spawn_params* p, int d, HANDLE h);
void spawn_param_redirect_inherit(spawn_params* p, int d);
#else
void spawn_param_redirect(spawn_params* p, int d, int fd);
void spawn_param_redirect_inherit(spawn_params* p, int d);
#endif
int spawn_param_execute(lua_State* L);

void close_proc_stdio_channel(process* p, int stdKind);
#endif