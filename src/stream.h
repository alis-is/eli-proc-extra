#include "lua.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
int stream_write(lua_State *L, HANDLE h, const char * data, size_t datasize, int nonblocking);
int stream_read_bytes(lua_State *L, HANDLE fd, size_t length) ;
int stream_read(lua_State *L, HANDLE fd, const char * opt, int nonblocking);
#else
int stream_write(lua_State *L, int fd, const char * data, size_t datasize, int nonblocking);
int stream_read_bytes(lua_State *L, int fd, size_t length) ;
int stream_read(lua_State *L, int fd, const char * opt, int nonblocking);
#endif