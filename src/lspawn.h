#ifndef ELI_SPAWN_H_
#define ELI_SPAWN_H_

#include <stdio.h>
#include "lprocess.h"
#include "lprocess_group.h"
#include "lua.h"
#include "stdioChannel.h"

#ifdef _WIN32
#include <windows.h>
#else
#include "environ.h"

#include <spawn.h>
#include <unistd.h>

#endif

typedef struct spawn_params {
    lua_State* L;
#ifdef _WIN32
    const char* cmdline;
    const char* environment;
    STARTUPINFO si;
#else
    const char *command, **argv, **envp;
    posix_spawn_file_actions_t redirect;
    posix_spawnattr_t attr;
#endif
    stdioChannel* stdio[3];
    int createProcessGroup;
    int process_group_ref;
} spawn_params;

int proc_create_meta(lua_State* L);

spawn_params* spawn_param_init(lua_State* L);
void spawn_param_filename(spawn_params* p, const char* filename);
void spawn_param_args(spawn_params* p);
void spawn_param_env(spawn_params* p);
#ifdef _WIN32
void spawn_param_redirect(spawn_params* p, int d, HANDLE h);
#else
void spawn_param_redirect(spawn_params* p, int d, int fd);
#endif
int spawn_param_execute(spawn_params* p);

void close_stdio_channel(process* p, int stdKind);
#endif