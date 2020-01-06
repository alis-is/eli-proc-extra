#include "lua.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

char *joinpath(const char *pth1, const char *pth2);
int push_error(lua_State *L, const char *info);
FILE *check_file(lua_State *L, int idx, const char *funcname);
int push_result(lua_State *L, int res, const char *info);
FILE *check_file(lua_State *L, int idx, const char *funcname);

#ifdef _WIN32
#include <ctype.h>
#include <windows.h>
int windows_pusherror(lua_State *L, DWORD error, int nresults);
#endif
