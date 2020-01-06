#include <stdio.h>
#include "lua.h"

#ifdef _WIN32
#include <windows.h>
#endif

struct process;
struct spawn_params;

int proc_create_meta(lua_State *L);

struct spawn_params *spawn_param_init(lua_State *L);
void spawn_param_filename(struct spawn_params *p, const char *filename);
void spawn_param_args(struct spawn_params *p);
void spawn_param_env(struct spawn_params *p);
#ifdef _WIN32
void spawn_param_redirect(struct spawn_params *p, const char *stdname, HANDLE h);
#else
void spawn_param_redirect(struct spawn_params *p, const char *stdname, int fd);
#endif
int spawn_param_execute(struct spawn_params *p);

int process_pid(lua_State *L);
int process_wait(lua_State *L);
int process_kill(lua_State *L);
int process_tostring(lua_State *L);
