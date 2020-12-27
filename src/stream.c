#include "lauxlib.h"
#include <stdio.h>
#include <errno.h>
#include "lutil.h"
#include "stream.h"

#ifdef _WIN32
int stream_write(lua_State *L, HANDLE h, const char * data, size_t datasize, int nonblocking)
#else
int stream_write(lua_State *L, int fd, const char * data, size_t datasize, int nonblocking)
#endif
{
    size_t status = 1;
#ifdef _WIN32
    DWORD dwBytesWritten = 0;
    DWORD bErrorFlag = WriteFile(h, data, datasize, &dwBytesWritten, NULL);
    if (bErrorFlag == FALSE)
    {
        DWORD err = GetLastError();
        if (!nonblocking || err != ERROR_IO_PENDING)
        { // nonblocking so np
            LPSTR messageBuffer = NULL;
            size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                            NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
            lua_pushnil(L);
            lua_pushstring(L, messageBuffer);
            lua_pushinteger(L, err);
            return 3;
        }
    }
    status = status && (dwBytesWritten == datasize);
#else
    status = status && (write(fd, data, datasize) == datasize);
    if (status == 0) 
    {
        return push_error(L, NULL);
    }
#endif
    
    lua_pushboolean(L, status);
    return 1;
}

#ifdef _WIN32
static DWORD read(HANDLE h, char *buffer, DWORD count)
{
    size_t res;
    DWORD lpNumberOfBytesRead = 0;
    DWORD bErrorFlag = ReadFile(h, buffer, LUAL_BUFFERSIZE, &lpNumberOfBytesRead, NULL);
    return bErrorFlag == FALSE ? -1 : lpNumberOfBytesRead;
}
#endif

static int push_read_result(lua_State *L, int res, int nonblocking)
{
    if (res == -1)
    {
        char *errmsg;
        size_t _errno;

#ifdef _WIN32
        if (!nonblocking || (_errno = GetLastError()) != ERROR_NO_DATA)
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK || !nonblocking)
#endif
        {
#ifdef _WIN32
            if (!nonblocking || _errno != ERROR_NO_DATA)
            { // nonblocking so np
                LPSTR messageBuffer = NULL;
                size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                             NULL, _errno, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
                errmsg = messageBuffer;
            }
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK || !nonblocking)
            {
                errmsg = strerror(errno);
                _errno = errno;
            }
#endif
            if (lua_rawlen(L, -1) == 0)
            {
                lua_pushnil(L);
            }
            lua_pushstring(L, errmsg);
            lua_pushinteger(L, _errno);
            return 3;
        }
    }
    return 1;
}

#ifdef _WIN32
    static int read_all(lua_State *L, HANDLE fd, int nonblocking)
#else
    static int read_all(lua_State *L, int fd, int nonblocking)
#endif
{
    size_t res;
    luaL_Buffer b;
    luaL_buffinit(L, &b);
    do
    { /* read file in chunks of LUAL_BUFFERSIZE bytes */
        char *p = luaL_prepbuffer(&b);
        res = read(fd, p, LUAL_BUFFERSIZE);
        if (res != -1)
            luaL_addlstring(&b, p, res);
    } while (res == LUAL_BUFFERSIZE);

    luaL_pushresult(&b); /* close buffer */
    return push_read_result(L, res, nonblocking);
}

#ifdef _WIN32
static int read_line(lua_State *L, HANDLE fd, int chop, int nonblocking)
#else
static int read_line(lua_State *L, int fd, int chop, int nonblocking)
#endif
{
    luaL_Buffer b;
    char c = '\0';
    luaL_buffinit(L, &b);
    size_t res = 1;

    while (res == 1 && c != EOF && c != '\n')
    {
        char *buff = luaL_prepbuffer(&b);
        int i = 0;
        while (i < LUAL_BUFFERSIZE && (res = read(fd, &c, sizeof(char))) == 1 && c != EOF && c != '\n')
        {
            buff[i++] = c;
        }
        if (res != -1)
            luaL_addsize(&b, i);
    }
    if (!chop && c == '\n')
        luaL_addchar(&b, c);
    luaL_pushresult(&b);

    return push_read_result(L, res, nonblocking);
}

#ifdef _WIN32
int stream_read_bytes(lua_State *L, HANDLE fd, size_t length) 
#else
int stream_read_bytes(lua_State *L, int fd, size_t length) 
#endif
{
    const char *result = malloc(sizeof(char) * length);

    size_t res = read(fd, result, length);
    if (res != -1)
    {
        lua_pushlstring(L, result, res);
        free(result);
        return 1;
    }
    free(result);
    return luaL_fileresult(L, res, NULL);
}

#ifdef _WIN32
int stream_read(lua_State *L, HANDLE fd, const char * opt, int nonblocking)
#else
int stream_read(lua_State *L, int fd, const char * opt, int nonblocking)
#endif
{
    size_t success;
    if (*opt == '*')
        opt++; /* skip optional '*' (for compatibility) */
    switch (*opt)
    {
    case 'l': /* line */
        return read_line(L, fd, 1, nonblocking);
    case 'L': /* line with end-of-line */
        return read_line(L, fd, 0, nonblocking);
    case 'a':
        return read_all(L, fd, nonblocking); /* read all data available */
    default:
        return luaL_argerror(L, 2, "invalid format");
    }
}